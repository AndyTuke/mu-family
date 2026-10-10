#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <atomic>

// TimedMidiOut — sends MIDI to a device port at given times, fed from a real-time thread.
// The producer (an audio callback or a render thread) only stamps each message with the time
// it is due and pushes it into a lock-free FIFO — no allocation, no MIDI system call; a sender
// thread sends each one when it falls due, in order. Windows timer resolution is 1 ms (JUCE
// sets it), which bounds the send jitter. Shared by mu-link's MIDI clock out and the mu-link
// bridge's MIDI out.
//
// Header-only and kept out of mu-core's INTERFACE sources on purpose: mu-link includes it
// without linking that library, and a translation unit that doesn't include it never starts
// its thread. Device MIDI output rules: docs/design-plugin-family.md "Device MIDI output".
//
// Port lifetime: TimedMidiOut never owns a port. The owner lends it with setOutput() and calls
// setOutput(nullptr) before deleting the port; that waits out any send in progress.
namespace mu_core
{

class TimedMidiOut : private juce::Thread
{
public:
    // The sender runs for the object's whole life, so the queue never fills with stale messages
    // while no port is set. Real-time priority where allowed (Linux needs rtprio), else highest.
    // Pass false to start it later with start() (an owner that may never send), or never (tests,
    // which drive sendDue() themselves).
    explicit TimedMidiOut(bool startSender = true) : juce::Thread("mu MIDI out")
    {
        if (startSender) start();
    }

    // Message thread: start the sender if it isn't running.
    void start()
    {
        if (! isThreadRunning() && ! startRealtimeThread(juce::Thread::RealtimeOptions{}.withPriority(8)))
            startThread(juce::Thread::Priority::highest);
    }
    ~TimedMidiOut() override { stopThread(1000); }

    // Message thread. nullptr stops sending; messages queued meanwhile are dropped as they fall due.
    void setOutput(juce::MidiOutput* output)
    {
        const juce::ScopedLock sl(portLock);
        out = output;
        portActive.store(output != nullptr, std::memory_order_relaxed);
    }

    bool isActive()  const { return portActive.load(std::memory_order_relaxed); }
    bool isSending() const { return isThreadRunning(); }

    // Producer (one thread per instance): queue a 1–3 byte message due at `dueMs`
    // (Time::getMillisecondCounterHiRes clock). Longer messages (SysEx) are dropped, as is
    // anything pushed while no port is set or when the queue is full.
    void push(double dueMs, const juce::uint8* bytes, int size)
    {
        if (! isActive() || size < 1 || size > 3)
            return;
        Event e { dueMs, { bytes[0], size > 1 ? bytes[1] : (juce::uint8) 0, size > 2 ? bytes[2] : (juce::uint8) 0 }, size };
        const auto scope = fifo.write(1);
        if (scope.blockSize1 > 0)
            events[(size_t) scope.startIndex1] = e;
    }

    // Producer: queue a whole block, each event due `samplePosition` samples after blockStartMs.
    void pushBuffer(const juce::MidiBuffer& midi, double blockStartMs, double sampleRate)
    {
        if (! isActive() || sampleRate <= 0.0)
            return;
        for (const auto meta : midi)
            push(blockStartMs + meta.samplePosition * 1000.0 / sampleRate, meta.data, meta.numBytes);
    }

    // Hands every queued message already due at nowMs to send(message, dueMs), in order. The
    // sender thread's step (the FIFO's one reader); tests call it on an instance built without
    // a sender.
    template <typename Send>
    void sendDue(double nowMs, Send&& send)
    {
        for (;;)
        {
            if (! hasHead)
            {
                const auto scope = fifo.read(1);
                if (scope.blockSize1 == 0) return;
                head    = events[(size_t) scope.startIndex1];
                hasHead = true;
            }
            if (head.dueMs > nowMs) return;   // the next message is not due yet: hold it
            send(head.size == 3 ? juce::MidiMessage(head.data[0], head.data[1], head.data[2])
               : head.size == 2 ? juce::MidiMessage(head.data[0], head.data[1])
                                : juce::MidiMessage(head.data[0]), head.dueMs);
            hasHead = false;
        }
    }

    // Tests only: let push() queue without a real port.
    void armForTest() { portActive.store(true, std::memory_order_relaxed); }

private:
    struct Event { double dueMs; juce::uint8 data[3]; int size; };

    // Sender: wake about every millisecond and send what is due.
    void run() override
    {
        while (! threadShouldExit())
        {
            {
                const juce::ScopedLock sl(portLock);
                sendDue(juce::Time::getMillisecondCounterHiRes(), [this](const juce::MidiMessage& m, double)
                {
                    if (out != nullptr) out->sendMessageNow(m);   // no port: dropped
                });
            }
            wait(1);
        }
    }

    static constexpr int kCapacity = 1024;
    std::array<Event, kCapacity> events {};
    juce::AbstractFifo fifo { kCapacity };
    Event head {};
    bool  hasHead = false;                    // reader only

    juce::CriticalSection portLock;           // sender + owner's thread only, never the producer
    juce::MidiOutput* out = nullptr;          // lent by the owner, never owned
    std::atomic<bool> portActive { false };   // producer: skip queueing with no port
};

} // namespace mu_core
