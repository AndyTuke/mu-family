#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <cstdint>
#include "Plugin/TimedMidiOut.h"   // mu-core: the timed sender (FIFO + sender thread)

// MIDI-clock-out bridge: turns the ServerEngine's per-block pulses into 0xF8 MIDI Clock
// bytes on an optional output port, so outboard gear locks to the same master clock as the
// in-app clients (design §3.1 — an OUTPUT derived from the master, never the internal
// mechanism). No port selected → a no-op.
//
// Transport: Start / Continue / Stop and Song Position Pointer go out at the start of the block
// where play starts or stops, ahead of its pulses, through the same queue so order is kept.
// This class only encodes the clock; timing and port lending are mu_core::TimedMidiOut's.
//
// Port lifetime: the port is owned by the AudioDeviceManager, which only swaps it after
// stopping the audio callbacks — so AudioServer clears it in audioDeviceStopped() and sets it
// again in audioDeviceAboutToStart(). This relies on the port being null whenever no audio
// device is open (every backend's stop() calls audioDeviceStopped()), because the manager
// deletes the port without callbacks then.
namespace mu_link
{

class MidiClockOut
{
public:
    explicit MidiClockOut(bool startSender = true) : sender(startSender) {}

    void setOutput(juce::MidiOutput* output) { sender.setOutput(output); }
    bool isSending() const { return sender.isSending(); }

    // Audio thread, once per block: `transport` (numTransport bytes: FA / FB / FC, F2 lsb msb,
    // F8) at the block start, then one 0xF8 per pulse, due `frameOffsets[i]` frames after
    // `blockStartMs`; pulses past numOffsets go at the block's last frame.
    void emit(double blockStartMs, const std::uint8_t* transport, int numTransport,
              const int* frameOffsets, int numOffsets, int numPulses, int numFrames, double sampleRate)
    {
        if (! sender.isActive() || sampleRate <= 0.0)
            return;
        // Transport messages: Song Position Pointer is three bytes, everything else one.
        for (int i = 0; i < numTransport;)
        {
            const int size = transport[i] == 0xF2 && i + 2 < numTransport ? 3 : 1;
            sender.push(blockStartMs, transport + i, size);
            i += size;
        }
        const juce::uint8 clock = 0xF8;
        for (int i = 0; i < numPulses; ++i)
        {
            const int at = i < numOffsets ? frameOffsets[i] : juce::jmax(0, numFrames - 1);
            sender.push(blockStartMs + at * 1000.0 / sampleRate, &clock, 1);
        }
    }

    template <typename Send>
    void sendDue(double nowMs, Send&& send) { sender.sendDue(nowMs, std::forward<Send>(send)); }
    void armForTest() { sender.armForTest(); }

private:
    mu_core::TimedMidiOut sender;
};

} // namespace mu_link
