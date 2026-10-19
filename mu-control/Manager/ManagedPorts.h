#pragma once

#include "Driver/DevicePorts.h"
#include <algorithm>
#include <array>
#include <functional>
#include <atomic>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <vector>

// The manager's wrapper round a device's ports: it moves everything that arrives on the MIDI or hot-plug
// thread onto the message thread, so the driver and the model (which are not thread-safe) only ever run
// there. Received bytes go through a lock-free single-producer FIFO and an AsyncUpdater; the MIDI thread
// touches nothing else. Connection changes are rare and come from a non-audio thread, so they use a small
// locked list. Everything pending is dropped on close, so nothing reaches a driver that has been closed.
namespace mu_control
{

class ManagedPorts : public DevicePorts, private juce::AsyncUpdater
{
public:
    explicit ManagedPorts(DevicePorts& innerPorts) : inner(innerPorts) {}
    ~ManagedPorts() override
    {
        // Stop the input thread before this object goes: it calls pushBytes / pushConnection on `this`.
        if (inner.isOpen()) inner.close();
        detach();
        cancelPendingUpdate();
    }

    juce::StringArray outputNames() const override { return inner.outputNames(); }
    juce::StringArray inputNames() const override  { return inner.inputNames(); }

    bool open(const PortSpec& spec) override
    {
        // The callbacks must be in place before the input starts.
        inner.onBytes = [this](const uint8_t* data, size_t size) { pushBytes(data, size); };
        inner.onConnectionChanged = [this](bool connected) { pushConnection(connected); };
        if (inner.open(spec)) return true;
        detach();
        return false;
    }

    void close() override
    {
        inner.close();   // stops the input thread first
        detach();
        discardPending();
    }

    bool isOpen() const override      { return inner.isOpen(); }
    bool isConnected() const override { return inner.isConnected(); }
    bool send(const uint8_t* data, size_t size) override { return inner.send(data, size); }
    void flush() override { inner.flush(); }

    // Called on the message thread after the driver has seen a connection change (the driver's own
    // onConnectionChanged runs first).
    std::function<void(bool)> connectionObserver;

private:
    static constexpr int    kSlots    = 256;
    static constexpr size_t kMaxBytes = 16;   // pad and button messages are 3 bytes; longer ones (SysEx replies) are dropped

    struct Slot
    {
        std::array<uint8_t, kMaxBytes> bytes {};
        uint8_t                        size = 0;
    };

    // MIDI thread: no allocation, no lock.
    void pushBytes(const uint8_t* data, size_t size)
    {
        if (size == 0 || size > kMaxBytes) return;
        {
            const auto w = fifo.write(1);   // the write is published when w goes out of scope
            if (w.blockSize1 == 0) return;   // full: drop rather than block the MIDI thread
            auto& slot = slots[(size_t) w.startIndex1];
            std::copy(data, data + size, slot.bytes.begin());
            slot.size = (uint8_t) size;
        }
        triggerAsyncUpdate();
    }

    void pushConnection(bool connected)
    {
        {
            const juce::ScopedLock lock(connectionLock);
            connectionEvents.push_back(connected);
        }
        triggerAsyncUpdate();
    }

    void handleAsyncUpdate() override
    {
        // Connection changes first, then every queued message, in arrival order. A callback may close the
        // ports, which empties both queues, so each pass re-reads them.
        for (;;)
        {
            bool connected = false, haveConnection = false;
            {
                const juce::ScopedLock lock(connectionLock);
                if (! connectionEvents.empty())
                {
                    connected = connectionEvents.front();
                    connectionEvents.erase(connectionEvents.begin());
                    haveConnection = true;
                }
            }
            if (! haveConnection) break;
            // Copies: a handler may close the ports, which clears these members while it is still running.
            const auto changed = onConnectionChanged;
            const auto observer = connectionObserver;
            if (changed) changed(connected);
            if (observer) observer(connected);
        }

        for (;;)
        {
            Slot slot;
            {
                const auto r = fifo.read(1);   // released when r goes out of scope
                if (r.blockSize1 == 0) break;
                slot = slots[(size_t) r.startIndex1];
            }
            const auto handler = onBytes;   // a copy, for the same reason
            if (handler) handler(slot.bytes.data(), slot.size);
        }
    }

    void detach()
    {
        inner.onBytes = nullptr;
        inner.onConnectionChanged = nullptr;
    }

    void discardPending()
    {
        cancelPendingUpdate();
        for (int n = fifo.getNumReady(); n > 0; --n) { const auto r = fifo.read(1); (void) r; }
        const juce::ScopedLock lock(connectionLock);
        connectionEvents.clear();
    }

    DevicePorts&                 inner;
    juce::AbstractFifo           fifo { kSlots };
    std::array<Slot, kSlots>     slots {};
    juce::CriticalSection        connectionLock;
    std::vector<bool>            connectionEvents;
};

} // namespace mu_control
