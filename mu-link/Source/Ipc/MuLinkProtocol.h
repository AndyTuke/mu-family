#pragma once

#include <atomic>
#include <cstdint>

// mu-link IPC contract — the shared-memory layout that the mu-link server and every
// connected mu client agree on. This header is the single source of truth for both
// sides; when the client glue is lifted into mu-core it will include this same file.
//
// Design (docs/mu-link/design-mulink.md): the server owns the one hardware audio
// device and runs the one callback. Each block it publishes a TransportBlock; each
// client renders ahead into its own SPSC AudioRing. The hardware word clock is the
// master clock, so sync is sample-accurate with no drift.
namespace mu_link
{

// Bump whenever the shared-memory layout changes incompatibly. A client whose
// version doesn't match the server's refuses to attach (and falls back to its own
// audio device) rather than reading a mismatched struct.
inline constexpr std::uint32_t kProtocolVersion = 1;

inline constexpr int kMaxClients     = 8;     // family is ≤8 products in practice
inline constexpr int kMaxChannels    = 2;     // stereo per client for now (see design §"channel layout")
inline constexpr int kMaxNameChars   = 32;    // client display name in the registry

// Base names for the named shared-memory regions (Win32 CreateFileMapping). The
// transport block and the client registry are global, one ring region per client.
inline constexpr const char* kTransportMapName = "Local\\mu_link_transport_v1";
inline constexpr const char* kRegistryMapName  = "Local\\mu_link_registry_v1";

// Master transport, published by the server once per audio block and read by every
// client. Fields are individually atomic; `generation` is bumped last (release) and
// read first (acquire) so a client can detect a torn mid-update read and retry.
struct TransportBlock
{
    std::atomic<std::uint64_t> generation { 0 };   // ++ every block; even = stable
    std::atomic<std::uint64_t> samplePos  { 0 };   // absolute frames since transport start
    std::atomic<double>        tempoBpm   { 120.0 };
    std::atomic<std::uint32_t> sampleRate { 0 };
    std::atomic<std::uint32_t> playing    { 0 };   // 0 = stopped, 1 = playing
    std::atomic<std::uint32_t> blockSize  { 0 };   // frames the server consumes per callback
};

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
