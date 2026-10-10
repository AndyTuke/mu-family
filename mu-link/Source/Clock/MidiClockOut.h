#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <atomic>
#include <cstdint>

// MIDI-clock-out bridge: turns the ServerEngine's per-block pulses into 0xF8 MIDI Clock
// bytes on an optional output port, so outboard gear locks to the same master clock as the
// in-app clients (design §3.1 — an OUTPUT derived from the master, never the internal
// mechanism). No port selected → a no-op.
//
// Transport: Start / Continue / Stop and Song Position Pointer go out at the start of the block
// where play starts or stops, ahead of its pulses, through the same queue so order is kept.
//
// Timing: the audio thread only stamps each pulse with its due time (callback start + its frame
// offset) and pushes it into a lock-free FIFO; a sender thread sends each byte when it is
// due, so pulses keep their spacing inside a block instead of bunching at its top, and no
// MIDI system call runs on the audio thread. Windows timer resolution is 1 ms (JUCE sets it),
// which bounds the send jitter.
//
// Port lifetime: the port is owned by the AudioDeviceManager, which only swaps it after
// stopping the audio callbacks — so AudioServer clears it in audioDeviceStopped() and sets it
// again in audioDeviceAboutToStart(); setOutput() waits out any send in progress. This relies on
// the port being null whenever no audio device is open (every backend's stop() calls
// audioDeviceStopped()), because the manager deletes the port without callbacks then.
namespace mu_link
{

class MidiClockOut : private juce::Thread
{
public:
    // The sender runs for the object's whole life, so the queue never fills with stale pulses
    // while no port is chosen. Real-time priority where allowed (Linux needs rtprio), else highest.
    // Tests pass false and drive sendDue() themselves.
    explicit MidiClockOut(bool startSender = true) : juce::Thread("mu-link MIDI clock out")
    {
        if (startSender && ! startRealtimeThread(juce::Thread::RealtimeOptions{}.withPriority(8)))
            startThread(juce::Thread::Priority::highest);
    }
    ~MidiClockOut() override { stopThread(1000); }

    // Message thread. nullptr stops sending; pulses queued meanwhile are dropped as they fall due.
    void setOutput(juce::MidiOutput* output)
    {
        const juce::ScopedLock sl(portLock);
        out = output;
        portActive.store(output != nullptr, std::memory_order_relaxed);
    }

    bool isSending() const { return isThreadRunning(); }

    // Audio thread, once per block: queue one 0xF8 per pulse, due `frameOffsets[i]` frames after
    // `blockStartMs` (the callback's start); pulses past numOffsets go at the block's last frame.
    // Lock-free, no allocation; nothing is queued while no port is set.
    // 	ransport (numTransport bytes: FA / FB / FC, F2 lsb msb, F8) goes first, at the block start.
    void emit(double blockStartMs, const std::uint8_t* transport, int numTransport,
              const int* frameOffsets, int numOffsets, int numPulses, int numFrames, double sampleRate)
    {
        if (! portActive.load(std::memory_order_relaxed) || sampleRate <= 0.0)
            return;
        // Transport messages: Song Position Pointer is three bytes, everything else one.
        for (int i = 0; i < numTransport;)
        {
            Event e { blockStartMs, { transport[i], 0, 0 }, 1 };
            if (transport[i] == 0xF2 && i + 2 < numTransport)
            {
                e.data[1] = transport[i + 1];
                e.data[2] = transport[i + 2];
                e.size    = 3;
            }
            push(e);
            i += e.size;
        }
        for (int i = 0; i < numPulses; ++i)
        {
            const int at = i < numOffsets ? frameOffsets[i] : juce::jmax(0, numFrames - 1);
            push({ blockStartMs + at * 1000.0 / sampleRate, { 0xF8, 0, 0 }, 1 });
        }
    }

    // Hands every queued message already due at nowMs to send(message, dueMs), in order. The sender
    // thread's step (the FIFO's one reader); tests call it on an instance built without a sender.
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
            if (head.dueMs > nowMs) return;   // the next byte is not due yet: hold it
            send(head.size == 3 ? juce::MidiMessage(head.data[0], head.data[1], head.data[2])
                                : juce::MidiMessage(head.data[0]), head.dueMs);
            hasHead = false;
        }
    }

    // Tests only: let emit() queue without a real port.
    void armForTest() { portActive.store(true, std::memory_order_relaxed); }

private:
    struct Event { double dueMs; juce::uint8 data[3]; int size; };

    void push(Event e)
    {
        const auto scope = fifo.write(1);
        if (scope.blockSize1 > 0)
            events[(size_t) scope.startIndex1] = e;
    }

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
    bool  hasHead = false;                 // reader only

    juce::CriticalSection portLock;        // sender + message thread only, never the audio thread
    juce::MidiOutput* out = nullptr;       // not owned (the AudioDeviceManager's default output)
    std::atomic<bool> portActive { false };   // audio thread: skip queueing with no port
};

} // namespace mu_link
