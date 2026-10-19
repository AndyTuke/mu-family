#pragma once

#include "Sequencer/HeldNoteStack.h"   // mu-core: the held-key stack
#include "Sequencer/RenderContext.h"   // mu-core: mu_core::MidiEvent

#include <cstdint>

// The keys a layer hears as held (root-by-MIDI and the MIDI-trigger run mode): the shared stack of held
// notes, newest on top, behind mu-Toni's MIDI decode. Plain data, owned by its layer and touched only by the
// audio thread, which feeds it every short MIDI message of every block the layer is in the processor for
// (rendered live or parked as a spare).
namespace mu_toni
{

class HeldNotes
{
public:
    static constexpr int kCapacity = 32;   // a 33rd distinct held note is not tracked

    // One short MIDI message. `inChannel` 0 takes every channel (Omni), 1..16 only that one. A note-on sets
    // `noteOnEdge` when `countsAsPress` (false for messages of an earlier, unrendered block: those keys are
    // tracked, but their press is not this block's).
    void apply(const mu_core::MidiEvent& e, int inChannel, bool countsAsPress, bool& noteOnEdge) noexcept
    {
        const uint8_t status = e.size > 0 ? e.bytes[0] : 0;
        const uint8_t type   = (uint8_t) (status & 0xf0);
        const int     channel = type != 0xf0 ? (status & 0x0f) + 1 : 0;   // 0: a system message (juce::MidiMessage::getChannel)
        if (inChannel > 0 && channel != inChannel) return;

        const int note = e.bytes[1];
        const int velocity = e.bytes[2];
        if (type == 0x90 && velocity != 0)
        {
            keys.push(note);
            if (countsAsPress) noteOnEdge = true;
        }
        else if (type == 0x80 || type == 0x90)          keys.remove(note);   // a note-off, or a note-on at velocity 0
        else if (type == 0xb0 && (note == 123 || note == 120)) keys.clear();   // all notes off / all sound off
    }

    bool anyHeld() const noexcept   { return keys.anyHeld(); }
    int  newest() const noexcept    { return keys.newest(); }   // -1: none held
    int  numHeld() const noexcept   { return keys.numHeld(); }

private:
    mu_core::HeldNoteStack<kCapacity, mu_core::HeldNoteOverflow::IgnoreNew> keys;
};

} // namespace mu_toni
