#pragma once

#include "Sequencer/Layer.h"          // mu-core: name, colour, modulation
#include "Sequencer/GatePattern.h"    // the drawable gate / filter / pitch envelopes
#include "Audio/VoiceRingBuffer.h"    // audio -> UI level tap for the sidebar glyph

#include <atomic>

namespace mu_tant
{

class PluginProcessor;

// One mu-Tant layer (a drone voice): the shared Layer (name, colour, modulation) plus everything
// that belongs to this voice alone — its three drawable envelopes, its user wavetables and the
// audio-to-UI level tap. The sound's parameters live in the APVTS (v{N}_*).
//
// Copying a Pattern (voice remove / swap) carries the voice's DATA — modulation, envelopes,
// user wavetable choice — and leaves the destination's own engine state alone (level tap, the
// stepped-pitch flags, the processor link), as the arrays it replaces were moved piecemeal.
struct Pattern : Layer
{
    Pattern() = default;
    Pattern(const Pattern& o) : Layer(o) { copyVoiceData(o); }
    Pattern& operator=(const Pattern& o)
    {
        if (this != &o) { Layer::operator=(o); copyVoiceData(o); }
        return *this;
    }

    GatePattern gate;          // amplitude gate
    GatePattern filterGate;    // filter-cutoff envelope
    GatePattern pitchGate;     // pitch envelope

    // User wavetable per oscillator: the file path (shown / saved) and its bank index (-1 = the
    // factory selection). The index is read by the audio thread, so it is atomic.
    juce::String     osc1UserPath, osc2UserPath;
    std::atomic<int> osc1UserIndex { -1 }, osc2UserIndex { -1 };

    // Whether a stepped (quantised) source drives each oscillator's semitone (audio-thread flag,
    // refreshed whenever the voice's modulators change).
    std::atomic<bool> osc1SemiStepped { false }, osc2SemiStepped { false };

    VoiceRingBuffer ring;      // mono tap of the voice's output for the sidebar animation

    OwnerLink<PluginProcessor> owner;
    void writeExtras(juce::ValueTree& node) const override;
    void applyExtras(const juce::ValueTree& node) override;

private:
    void copyVoiceData(const Pattern& o)
    {
        // A skipped copy (the source envelope was being edited for too long) would leave stale data.
        [[maybe_unused]] const bool copied = gate.copyDataFrom(o.gate)
                                           & filterGate.copyDataFrom(o.filterGate)
                                           & pitchGate.copyDataFrom(o.pitchGate);
        jassert(copied);
        osc1UserPath = o.osc1UserPath;
        osc2UserPath = o.osc2UserPath;
        osc1UserIndex.store(o.osc1UserIndex.load());
        osc2UserIndex.store(o.osc2UserIndex.load());
    }
};

} // namespace mu_tant
