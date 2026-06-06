#pragma once

#include "Link/MuLinkProtocol.h"
#include "Link/AudioRing.h"
#include "Link/MuLinkSharedMemory.h"   // SharedMemoryRegion

#include <new>

// MuLinkServerMemory — the SERVER half of the shared-memory bus, owned by mu-link only.
// (The contract, the ring, the region wrapper, and the client half all live in mu-core's
// Link module so every product's standalone can attach; only this server composer is
// mu-link-specific.) Creates and owns the transport block, the registry, and all per-slot
// rings, constructing the shared objects into freshly-created regions.
namespace mu_link
{

#ifdef _WIN32

class MuLinkServerMemory
{
public:
    // Create every region and construct the shared objects into freshly-created ones.
    // Returns false if any mapping failed (then valid()==false and nothing is published).
    bool create()
    {
        bool freshTransport = false, freshRegistry = false;
        transportRegion = SharedMemoryRegion::create(kTransportMapName, sizeof(TransportBlock), freshTransport);
        registryRegion  = SharedMemoryRegion::create(kRegistryMapName,  sizeof(ClientRegistry), freshRegistry);
        if (! transportRegion.valid() || ! registryRegion.valid())
            return false;

        // Construct (or adopt) the shared objects. Only stamp a freshly-created region —
        // never clobber state another running instance already published.
        if (freshTransport) new (transportRegion.data()) TransportBlock{};
        if (freshRegistry)
        {
            auto* reg = new (registryRegion.data()) ClientRegistry{};
            reg->protocolVersion.store(kProtocolVersion, std::memory_order_release);
        }

        const std::size_t ringBytes = AudioRingView::bytesFor(kMaxChannels, kRingCapacityFrames);
        for (int slot = 0; slot < kMaxClients; ++slot)
        {
            bool freshRing = false;
            ringRegions[slot] = SharedMemoryRegion::create(ringMapName((std::uint32_t) slot), ringBytes, freshRing);
            if (! ringRegions[slot].valid())
                return false;
            if (freshRing)
                AudioRingView::layout(ringHeader(slot), kMaxChannels, kRingCapacityFrames);
        }
        return true;
    }

    bool valid() const noexcept { return transportRegion.valid() && registryRegion.valid(); }

    TransportBlock& transport() noexcept { return *reinterpret_cast<TransportBlock*>(transportRegion.data()); }
    ClientRegistry& registry()  noexcept { return *reinterpret_cast<ClientRegistry*>(registryRegion.data()); }

    // The server's (consumer) view over slot `slot`'s ring.
    AudioRingView ring(int slot) noexcept { return AudioRingView(ringHeader(slot), ringData(slot)); }

private:
    AudioRingHeader* ringHeader(int slot) noexcept
    {
        return reinterpret_cast<AudioRingHeader*>(ringRegions[slot].data());
    }
    float* ringData(int slot) noexcept
    {
        return reinterpret_cast<float*>(reinterpret_cast<char*>(ringRegions[slot].data()) + sizeof(AudioRingHeader));
    }

    SharedMemoryRegion transportRegion;
    SharedMemoryRegion registryRegion;
    SharedMemoryRegion ringRegions[kMaxClients];
};

#endif // _WIN32

} // namespace mu_link
