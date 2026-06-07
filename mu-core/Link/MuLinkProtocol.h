#pragma once

#include <atomic>
#include <cstdint>
#include <string>

// mu-link IPC contract — the shared-memory layout the mu-link server and every connected
// mu client agree on. This is the single source of truth for both sides. It lives in
// mu-core (not mu-link) because the CLIENT half ships inside every product's standalone
// via MuLinkClient; the mu-link server includes the very same header. Plugin-agnostic:
// pure POD + atomics, no product symbols, so it satisfies the mu-core boundary rule.
//
// Design (docs/mu-link/design-mulink.md): the server owns the one hardware audio device
// and runs the one callback. Each block it publishes a TransportBlock; each client renders
// ahead into its own SPSC AudioRing. The hardware word clock is the master clock, so sync
// is sample-accurate with no drift.
namespace mu_link
{

// Bump whenever the shared-memory layout changes incompatibly. A client whose version
// doesn't match the server's refuses to attach (and falls back to its own audio device)
// rather than reading a mismatched struct.
inline constexpr std::uint32_t kProtocolVersion = 1;

inline constexpr int kMaxClients     = 8;     // family is ≤8 products in practice
inline constexpr int kMaxChannels    = 2;     // stereo per client for now (see design §"channel layout")
inline constexpr int kMaxNameChars   = 32;    // client display name in the registry

// Base names for the named shared-memory regions (Win32 CreateFileMapping). The
// transport block and the client registry are global, one ring region per client.
inline constexpr const char* kTransportMapName = "Local\\mu_link_transport_v1";
inline constexpr const char* kRegistryMapName  = "Local\\mu_link_registry_v1";
inline constexpr const char* kRingMapPrefix    = "Local\\mu_link_ring_v1_";  // + clientId

// The named mapping for client `clientId`'s audio ring. Server and client derive the
// same name from the registry-assigned id, so the client never has to be told it.
inline std::string ringMapName(std::uint32_t clientId)
{
    return std::string(kRingMapPrefix) + std::to_string(clientId);
}

// Default per-client ring geometry. The capacity is several audio blocks deep so the
// async double-buffered producer can run ahead of the server's consume without ever
// underrunning under normal scheduling (design §3.2). Sizing the shared region only
// needs these + AudioRingView::bytesFor().
inline constexpr int kRingCapacityFrames = 8192;   // ≈ 170 ms @ 48 kHz; ample run-ahead

// Master transport, published by the server once per audio block and read by every
// client. Fields are individually atomic; `generation` is the seqlock guard — the
// server bumps it to odd before writing and to even after, so a reader takes a snapshot
// only between two equal even reads (see writeTransport / readTransport below).
struct TransportBlock
{
    std::atomic<std::uint64_t> generation  { 0 };   // seqlock: even = stable, odd = writing
    std::atomic<std::uint64_t> samplePos   { 0 };   // absolute frames since transport start
    std::atomic<double>        ppqPosition { 0.0 }; // accumulated quarter notes (tempo-change safe)
    std::atomic<double>        tempoBpm    { 120.0 };
    std::atomic<std::uint32_t> sampleRate  { 0 };
    std::atomic<std::uint32_t> playing     { 0 };   // 0 = stopped, 1 = playing
    std::atomic<std::uint32_t> blockSize   { 0 };   // frames the server consumes per callback
};

// A consistent copy of the transport for a client to render against.
struct TransportSnapshot
{
    std::uint64_t samplePos   = 0;
    double        ppqPosition = 0.0;   // musical position; carried, not re-derived from samples
    double        tempoBpm    = 120.0;
    std::uint32_t sampleRate  = 0;
    std::uint32_t playing     = 0;
    std::uint32_t blockSize   = 0;
};

// Server side: publish a snapshot under the seqlock. Bump to odd (write in progress),
// store the fields, bump to even (stable). The two release stores bracket the payload
// so a reader can never observe a half-updated block.
inline void writeTransport(TransportBlock& t, const TransportSnapshot& s) noexcept
{
    const std::uint64_t g = t.generation.load(std::memory_order_relaxed);
    t.generation.store(g + 1, std::memory_order_release);          // odd: writing
    std::atomic_thread_fence(std::memory_order_release);
    t.samplePos  .store(s.samplePos,   std::memory_order_relaxed);
    t.ppqPosition.store(s.ppqPosition, std::memory_order_relaxed);
    t.tempoBpm   .store(s.tempoBpm,    std::memory_order_relaxed);
    t.sampleRate .store(s.sampleRate,  std::memory_order_relaxed);
    t.playing    .store(s.playing,     std::memory_order_relaxed);
    t.blockSize  .store(s.blockSize,   std::memory_order_relaxed);
    t.generation.store(g + 2, std::memory_order_release);          // even: stable
}

// Client side: take a torn-free snapshot. Spin while a write is in progress (odd) or the
// generation moved under us. Lock-free and wait-free in practice (the writer holds the
// odd state for only a handful of relaxed stores).
//
// The spin is BOUNDED: if the writer never reaches a stable even generation within
// kMaxSpins (the mu-link server crashed mid-write, leaving generation odd forever) we fail
// safe to a stopped snapshot rather than hang the caller's render thread. The bridge's
// generation-stall watchdog then detaches the client back to its own device.
inline TransportSnapshot readTransport(const TransportBlock& t) noexcept
{
    constexpr int kMaxSpins = 4096;   // ample: the writer holds odd for ~5 relaxed stores
    for (int spin = 0; spin < kMaxSpins; ++spin)
    {
        const std::uint64_t g1 = t.generation.load(std::memory_order_acquire);
        if ((g1 & 1u) != 0u)
            continue;                                              // mid-write, retry
        TransportSnapshot s;
        s.samplePos   = t.samplePos  .load(std::memory_order_relaxed);
        s.ppqPosition = t.ppqPosition.load(std::memory_order_relaxed);
        s.tempoBpm    = t.tempoBpm   .load(std::memory_order_relaxed);
        s.sampleRate  = t.sampleRate .load(std::memory_order_relaxed);
        s.playing     = t.playing    .load(std::memory_order_relaxed);
        s.blockSize   = t.blockSize  .load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (t.generation.load(std::memory_order_acquire) == g1)
            return s;                                             // stable across the read
    }
    return TransportSnapshot{};   // writer stuck (server died mid-write) → fail safe to stopped
}

// One row of the client registry — how a client announces itself to the server and
// how the server reports liveness back. `heartbeat` is bumped by the owning side so
// the server can reap a client that died without detaching.
struct ClientSlot
{
    std::atomic<std::uint32_t> active     { 0 };   // 0 = free slot, 1 = claimed
    std::atomic<std::uint32_t> clientId   { 0 };   // unique per attach
    std::atomic<std::uint32_t> numChannels{ 0 };
    std::atomic<std::uint64_t> heartbeat  { 0 };   // liveness counter
    char                       name[kMaxNameChars] { };
};

struct ClientRegistry
{
    std::atomic<std::uint32_t> protocolVersion { kProtocolVersion };
    ClientSlot                 slots[kMaxClients];
};

} // namespace mu_link
