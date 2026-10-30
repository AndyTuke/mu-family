#pragma once

#include "Link/MuLinkProtocol.h"
#include "Link/AudioRing.h"

#include <cstring>
#include <string>

// Win32 shared-memory primitives for the mu-link bus — the CLIENT half + the generic
// region wrapper, both used by every product's standalone via MuLinkClient. (The server
// composer, MuLinkServerMemory, lives in mu-link itself; it includes this header for
// SharedMemoryRegion.) Plugin-agnostic, so it satisfies the mu-core boundary rule.
//
// Auto-detection: a client OPENS the well-known named mappings (OpenFileMapping). Success
// means mu-link is running; failure means run alone. Probing the names IS the discovery
// handshake — no config, any launch order. Ring geometry is fixed at (kMaxChannels,
// kRingCapacityFrames) per slot so the server can create all rings up front; the live
// read/write geometry always comes from the ring header.
namespace mu_link
{

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>

// RAII over one named Win32 file-mapping and its mapped view. Move-only; unmaps the
// view then closes the handle on destruction.
class SharedMemoryRegion
{
public:
    SharedMemoryRegion() = default;
    ~SharedMemoryRegion() { reset(); }

    SharedMemoryRegion(SharedMemoryRegion&& o) noexcept { moveFrom(o); }
    SharedMemoryRegion& operator=(SharedMemoryRegion&& o) noexcept
    {
        if (&o != this) { reset(); moveFrom(o); }
        return *this;
    }
    SharedMemoryRegion(const SharedMemoryRegion&)            = delete;
    SharedMemoryRegion& operator=(const SharedMemoryRegion&) = delete;

    // Server: create a named region of `size` bytes (or open it if it already exists).
    // `createdFresh` reports whether THIS call created it — so the caller knows whether
    // to construct the shared objects or leave an existing instance's state untouched.
    static SharedMemoryRegion create(const std::string& name, std::size_t size, bool& createdFresh)
    {
        SharedMemoryRegion r;
        const DWORD hi = (DWORD) ((std::uint64_t) size >> 32);
        const DWORD lo = (DWORD) ((std::uint64_t) size & 0xffffffffull);
        r.mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, hi, lo, name.c_str());
        createdFresh = (r.mapping != nullptr && GetLastError() != ERROR_ALREADY_EXISTS);
        if (r.mapping == nullptr) return r;
        r.base = MapViewOfFile(r.mapping, FILE_MAP_ALL_ACCESS, 0, 0, size);
        r.size = size;
        if (r.base == nullptr) r.reset();
        return r;
    }

    // Client: open an existing named region. Invalid (valid()==false) when mu-link isn't
    // running — the caller then stays on its own audio device.
    static SharedMemoryRegion open(const std::string& name, std::size_t size)
    {
        SharedMemoryRegion r;
        r.mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
        if (r.mapping == nullptr) return r;
        r.base = MapViewOfFile(r.mapping, FILE_MAP_ALL_ACCESS, 0, 0, size);
        r.size = size;
        if (r.base == nullptr) r.reset();
        return r;
    }

    bool        valid() const noexcept { return base != nullptr; }
    void*       data()  const noexcept { return base; }
    std::size_t bytes() const noexcept { return size; }

private:
    void reset() noexcept
    {
        if (base    != nullptr) { UnmapViewOfFile(base); base = nullptr; }
        if (mapping != nullptr) { CloseHandle(mapping);  mapping = nullptr; }
        size = 0;
    }
    void moveFrom(SharedMemoryRegion& o) noexcept
    {
        mapping = o.mapping; base = o.base; size = o.size;
        o.mapping = nullptr; o.base = nullptr; o.size = 0;
    }

    HANDLE      mapping = nullptr;
    void*       base    = nullptr;
    std::size_t size    = 0;
};

// Client side: opens the server's regions, version-checks, and (optionally) claims a
// registry slot + that slot's ring to become a producer. Low-level mapping handle; the
// product-facing lifecycle + render thread lives in MuLinkClient.
class MuLinkClientMemory
{
public:
    // Attach to a running mu-link. Returns false when mu-link isn't running OR the
    // protocol version doesn't match (the client then falls back to its own device).
    bool open()
    {
        transportRegion = SharedMemoryRegion::open(transportMapName(), sizeof(TransportBlock));
        registryRegion  = SharedMemoryRegion::open(registryMapName(),  sizeof(ClientRegistry));
        if (! transportRegion.valid() || ! registryRegion.valid())
            return false;
        if (registry().protocolVersion.load(std::memory_order_acquire) != kProtocolVersion)
        {
            transportRegion = SharedMemoryRegion{};   // refuse: incompatible layout
            registryRegion  = SharedMemoryRegion{};
            return false;
        }
        return true;
    }

    bool valid() const noexcept { return transportRegion.valid() && registryRegion.valid(); }

    const TransportBlock& transport() const noexcept { return *reinterpret_cast<const TransportBlock*>(transportRegion.data()); }
    ClientRegistry&       registry()        noexcept { return *reinterpret_cast<ClientRegistry*>(registryRegion.data()); }
    const ClientRegistry& registry()  const noexcept { return *reinterpret_cast<const ClientRegistry*>(registryRegion.data()); }

    // Claim the first free registry slot (atomic compare-exchange on `active`, which then holds this
    // attach's unique token) and open that slot's ring as the producer. Returns the slot index, or
    // -1 if none could be claimed.
    //
    // A freed slot's ring can still hold the previous tenant's unread audio, which the new producer
    // would otherwise play. Only the consumer (the server) may move readPos, and it drains every free
    // slot's ring each block, so a slot whose ring is not yet empty is skipped until that has happened.
    int claimSlot(const char* displayName, int numChannels)
    {
        auto& reg = registry();
        const std::size_t ringBytes = AudioRingView::bytesFor(kMaxChannels, kRingCapacityFrames);

        for (int slot = 0; slot < kMaxClients; ++slot)
        {
            auto& s = reg.slots[slot];
            if (s.active.load(std::memory_order_acquire) != 0)
                continue;                                   // held by a live client

            SharedMemoryRegion ringMap = SharedMemoryRegion::open(ringMapName((std::uint32_t) slot), ringBytes);
            if (! ringMap.valid() || ringViewOf(ringMap).readAvailable() > 0)
                continue;                                   // ring missing, or stale audio not drained yet

            std::uint32_t token = reg.nextAttachToken.fetch_add(1, std::memory_order_relaxed);
            if (token == 0)
                token = reg.nextAttachToken.fetch_add(1, std::memory_order_relaxed);   // 0 means free; skip it on wrap

            std::uint32_t expected = 0;
            if (! s.active.compare_exchange_strong(expected, token, std::memory_order_acq_rel))
                continue;                                   // another client took the slot first

            s.clientId.store((std::uint32_t) slot + 1, std::memory_order_relaxed);
            s.numChannels.store((std::uint32_t) numChannels, std::memory_order_relaxed);
            s.heartbeat.store(0, std::memory_order_relaxed);
            s.pcEpoch.store(0, std::memory_order_relaxed);    // start clean: no stale scene PC
            s.pcProgram.store(0, std::memory_order_relaxed);
            s.pcChannel.store(9, std::memory_order_relaxed);
            // requestedDepthFrames is the server's: a claim must not erase a request made as the slot went
            // active, so it is left alone (the server re-sends its choice while the slot is active).
            s.underrunFrames.store(0, std::memory_order_relaxed);   // a new client starts clean
            std::memset(s.name, 0, sizeof(s.name));
            for (int i = 0; displayName != nullptr && displayName[i] != '\0' && i < kMaxNameChars - 1; ++i)
                s.name[i] = displayName[i];   // bounded copy, always NUL-terminated

            ringRegion  = std::move(ringMap);
            claimedSlot = slot;
            ownToken    = token;
            return slot;
        }
        return -1;
    }

    // Release the claimed slot back to the pool (marks it free; closes the ring view). Frees the slot
    // only if it is still ours: after the server reaped it (a stall of two seconds or more) it may
    // already belong to a new client, whose claim must not be wiped.
    void releaseSlot()
    {
        if (claimedSlot >= 0)
        {
            std::uint32_t expected = ownToken;
            registry().slots[claimedSlot].active.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
            ringRegion  = SharedMemoryRegion{};
            claimedSlot = -1;
            ownToken    = 0;
        }
    }

    // True while the claimed slot is still this client's: false once the server reaped it (the slot
    // is free or another client's). The producer checks it before every write so it can never become
    // a second producer on a ring that now belongs to someone else. Any thread.
    bool ownsSlot() const noexcept
    {
        return claimedSlot >= 0
            && registry().slots[claimedSlot].active.load(std::memory_order_acquire) == ownToken;
    }

    // Bump the claimed slot's liveness counter so the server can reap a client that died
    // without detaching (L6). No-op before claimSlot().
    void bumpHeartbeat() noexcept
    {
        if (ownsSlot())   // a stalled client must not keep a new tenant's slot looking alive
            registry().slots[claimedSlot].heartbeat.fetch_add(1, std::memory_order_relaxed);
    }

    int claimedSlotIndex() const noexcept { return claimedSlot; }

    // The client's (producer) view over its claimed ring. Invalid before claimSlot().
    AudioRingView ring() noexcept
    {
        if (claimedSlot < 0) return {};
        return ringViewOf(ringRegion);
    }

private:
    // A view over a mapped ring region: [AudioRingHeader][interleaved float data].
    static AudioRingView ringViewOf(SharedMemoryRegion& region) noexcept
    {
        auto* hdr  = reinterpret_cast<AudioRingHeader*>(region.data());
        auto* data = reinterpret_cast<float*>(reinterpret_cast<char*>(region.data()) + sizeof(AudioRingHeader));
        return AudioRingView(hdr, data);
    }

    SharedMemoryRegion transportRegion;
    SharedMemoryRegion registryRegion;
    SharedMemoryRegion ringRegion;
    int                claimedSlot = -1;
    std::uint32_t      ownToken    = 0;   // the `active` value this client's claim wrote (0 = none)
};

#endif // _WIN32

} // namespace mu_link
