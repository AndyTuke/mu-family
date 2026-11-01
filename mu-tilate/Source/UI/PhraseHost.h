#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

// What the Phrase layer's UI (PhraseContent) needs from whoever hosts the layer (docs/mu-tronic/design.md 2.2):
// the parameters, the layer object, and the loop-loading service. mu-Tilate's PluginProcessor implements it; a host of
// several layer types implements one per type. Message thread only.
namespace mu_tilate
{

struct Phrase;

class PhraseHost
{
public:
    virtual ~PhraseHost() = default;

    // The parameter tree the layer's p{N}_ rows live in.
    virtual juce::AudioProcessorValueTreeState& getPhraseParameters() noexcept = 0;
    // Layer `layer` as its Phrase (its loop data and modulation matrix).
    virtual Phrase& getPhrase(int layer) noexcept = 0;
    // Post-matrix value of modulation target `dest` on layer `layer`, in the target's parameter units.
    virtual float getModLive(int layer, int dest) const noexcept = 0;
    virtual int   getPhraseColourIndex(int layer) const = 0;

    // The host transport as the content shows it: playing, and the beat it is at.
    virtual bool   isPhraseTransportPlaying() const noexcept = 0;
    virtual double getPhraseTransportBeat() const noexcept = 0;

    // Decodes `file` on a worker, detects its slices and publishes them to the layer with an empty timeline (the sequence is
    // the user's to build, or to lay out with layOutSlices). A newer request for the same layer supersedes an older one. `keepSavedEdits` is for a
    // session restore: the slices and timeline the saved file carried are kept when they were made on a loop of the
    // same length, instead of being detected afresh.
    virtual void loadLoopAsync(int layer, const juce::File& file, bool keepSavedEdits = false) = 0;
    // Runs the detector again on the layer's loaded loop with the layer's current detection settings (pinned markers
    // stay) and lays the new slices out in their original order, replacing the timeline.
    virtual void redetectSlices(int layer) = 0;
    // Lays the layer's slices out one after another in source order from beat 0, replacing the hits, and sizes the sequence to
    // fit (in whole bars). A loaded loop starts with an empty timeline; this is the one-press way to a starting sequence.
    virtual void layOutSlices(int layer) = 0;
    // One line for the content: the loop's name and slice count, "Loading ...", or the last error.
    virtual juce::String getLoopStatus(int layer) const = 0;
};

} // namespace mu_tilate
