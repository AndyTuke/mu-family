#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <cstdint>

// MIDI-clock-out bridge: turns the per-block 24-ppqn pulse delta from the ServerEngine
// into 0xF8 MIDI Clock bytes on an optional output port, so outboard gear locks to the
// same master clock as the in-app clients (design §3.1 — an OUTPUT derived from the
// master, never the internal mechanism). No port selected → a no-op.
//
// Sample-accurate placement of the F8 bytes within the block is a later refinement; for
// now the block's pulses are emitted at the top of the callback, which is well within a
// MIDI-clock receiver's tolerance.
namespace mu_link
{

class MidiClockOut
{
public:
    void setOutput(juce::MidiOutput* output) noexcept { out = output; }
    bool hasOutput() const noexcept { return out != nullptr; }

    // Emit `pulses` MIDI Clock messages (one 0xF8 each). Called once per audio block with
    // the clock's pulse delta for that block.
    void emit(std::uint64_t pulses) const
    {
        if (out == nullptr)
            return;
        for (std::uint64_t i = 0; i < pulses; ++i)
            out->sendMessageNow(juce::MidiMessage::midiClock());
    }

private:
    juce::MidiOutput* out = nullptr;   // not owned
};

} // namespace mu_link
