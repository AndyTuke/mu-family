#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Persistence/TrackParams.h"   // EngineType

// What the Track layer's UI (TrackContent, StepRows) needs from whoever hosts the layer (docs/mu-tronic/
// design.md 2.2): the parameters, the layer objects, which engine each runs, the live modulation values and
// the few layer-list and transport readings the content shows. mu-On's PluginProcessor implements it; a host
// of several layer types implements one per type. Message thread only: the content's timers and controls
// call these.
namespace mu_on
{

struct Track;

class TrackHost
{
public:
    virtual ~TrackHost() = default;

    // The parameter tree the layer's t{N}_ rows and the track's groove rows (t{N}_swing, t{N}_accent) live in.
    virtual juce::AudioProcessorValueTreeState& getTrackParameters() noexcept = 0;
    // Layer `layer` as its Track (step row, Rumble envelope, modulation matrix).
    virtual Track& getTrack(int layer) noexcept = 0;
    // How many layers are live, and what the list calls layer `layer` (its engine's name).
    virtual int          getTrackCount() const = 0;
    virtual juce::String getTrackName(int layer) const = 0;
    // The layer's palette colour index (MuLookAndFeel::layerPalette).
    virtual int getTrackColourIndex(int layer) const = 0;
    // The engine layer `layer` runs, and switching it (every engine keeps its settings; see
    // PluginProcessor::selectTrackEngine).
    virtual EngineType getTrackEngine(int layer) const noexcept = 0;
    virtual void       selectTrackEngine(int layer, EngineType engine) = 0;
    // Post-matrix value of modulation target `dest` (a row of `engine`'s table) on layer `layer`, in the
    // target's parameter units; NaN while the layer runs another engine.
    virtual float getModLive(int layer, EngineType engine, int dest) const noexcept = 0;
    // The transport as the content shows it: playing, and the beat it is at.
    virtual bool   isTrackTransportPlaying() const noexcept = 0;
    virtual double getTrackTransportBeat() const noexcept = 0;
};

} // namespace mu_on
