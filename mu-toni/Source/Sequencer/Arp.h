#pragma once

#include "Sequencer/ArpVoiceRunner.h"   // per-layer arpeggiator + ToniVoice
#include "Sequencer/HeldNotes.h"        // the keys this layer hears
#include "Sequencer/Layer.h"            // mu-core: name, colour, modulation
#include "Audio/InsertProcessor.h"      // mu-core: shared per-voice insert FX
#include "Modulation/MuToniModDest.h"   // isValidModDest

namespace mu_toni
{

// One mu-Toni layer: the shared Layer (name, colour, modulation) plus its own arp voice and
// insert effect. `insertCfg` holds the insert's algorithm + slot params, set per block.
//
// The keys it hears (root-by-MIDI, MIDI-trigger) are its own state: `heldNotes`, and `context`, the block's
// arp context the runner renders with. Audio thread only: hearMidi runs inside the block's layer access,
// for every slot's Arp (spares included), so a layer that goes live already knows which keys are down.
struct Arp : Layer
{
    ArpVoiceRunner  runner;
    SlotBound<InsertProcessor> insert;   // belongs to the slot: a layer copy never carries its delay lines
    VoiceParams     insertCfg;
    HeldNotes       heldNotes;           // the keys this layer hears as held, newest on top
    ArpContext      context;             // this block's arp context: the transport plus this layer's held keys

    // Audio thread: this block's MIDI into the held keys, and the key fields of `context` from them. The first
    // `carried` events are from earlier blocks that rendered nothing: their keys count, their presses do not.
    // `inChannel` 0 hears every channel (Omni), 1..16 only that one.
    void hearMidi(const mu_core::MidiEvent* events, int count, int carried, int inChannel) noexcept
    {
        context.noteOnEdge = false;
        for (int i = 0; i < count; ++i)
            heldNotes.apply(events[i], inChannel, i >= carried, context.noteOnEdge);
        context.anyNoteHeld      = heldNotes.anyHeld();
        context.rootOverrideMidi = heldNotes.newest();
    }

    // The compressor / limiter meter of this layer's insert.
    const std::atomic<float>* insertGainReduction() const override { return &insert->grReduction; }

    // Saved modulators keep only destinations mu-Toni has.
    bool isValidDest(const std::string& id) const override { return isValidModDest(id); }
};

} // namespace mu_toni
