#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

// What the Arp layer's UI (ArpContent) needs from whoever hosts the layer (docs/mu-tronic/design.md 2.2):
// the parameters, the layer object, the live modulation values and the few transport and mixer readings
// the content shows. mu-Toni's PluginProcessor implements it; a host of several layer types implements
// one per type. Message thread only: the content's timer and its controls call these.
namespace mu_toni
{

struct Arp;

class ArpHost
{
public:
    virtual ~ArpHost() = default;

    // The parameter tree the layer's a{N}_ rows live in.
    virtual juce::AudioProcessorValueTreeState& getArpParameters() noexcept = 0;
    // Layer `layer` as its Arp (its modulation matrix binds the knobs' live arcs).
    virtual Arp& getArp(int layer) noexcept = 0;
    // Post-matrix value of modulation target `dest` on layer `layer`, in the target's parameter units.
    virtual float getModLive(int layer, int dest) const noexcept = 0;
    // The layer's palette colour index (MuLookAndFeel::layerPalette).
    virtual int getArpColourIndex(int layer) const = 0;
    // The layer's insert gain-reduction meter, or null when it shows none.
    virtual const std::atomic<float>* getArpInsertGainReduction(int layer) const = 0;
    // The host transport as the content shows it: playing, and the beat it is at.
    virtual bool   isArpTransportPlaying() const noexcept = 0;
    virtual double getArpTransportBeat() const noexcept = 0;
    // The names of the wavetables the oscillators can pick (the shared bank's tables, in index order).
    virtual juce::StringArray getWavetableNames() const = 0;
};

} // namespace mu_toni
