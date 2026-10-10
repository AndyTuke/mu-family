#pragma once

#include <juce_core/juce_core.h>

namespace mu_clid {

class PluginProcessor;
class Rhythm;

// mu-Clid's rhythm-slot management (message thread): add, remove, swap, reset and rename.
// Each operation keeps the processor's parallel per-rhythm state in step — sequencer
// rhythm, voice + MIDI engines, mixer channel, sample path, UI play-state, APVTS and any
// staged hot-swap — and serialises with the audio thread through suspendProcessing +
// rhythmsLock. Owned by PluginProcessor as `rhythms`; the engine arrays stay on the
// processor because the audio path reads them directly.
class RhythmManager
{
public:
    explicit RhythmManager(PluginProcessor& p) : proc_(p) {}

    void add   (const Rhythm& r);
    void remove(int index);
    // Swaps two active slots (the sidebar drag-reorder); false when either index is invalid.
    bool swap  (int i, int j);
    // Resets a rhythm to defaults, keeping its name and colour.
    void reset (int index);
    void rename(int index, const juce::String& newName);

private:
    void resetPlayState(int index);

    PluginProcessor& proc_;
};

} // namespace mu_clid
