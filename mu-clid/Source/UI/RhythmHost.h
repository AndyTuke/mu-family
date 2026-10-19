#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

#include "Sequencer/GeneratorAliases.h"   // Rhythm, RhythmPlayState, EuclidOverrides

// What the Rhythm layer's UI needs from whoever hosts it (docs/mu-tronic/design.md 2.2): RhythmContent, its
// voice and MIDI Out sections, the sample browser and the sidebar's mini circle read the parameters, the
// rhythms, the play state, the live modulation values, the insert meter, the rhythm presets and the sample
// library through it. mu-Clid's PluginProcessor implements it; a host of several layer types implements one
// per type. The method names carry the type, so one class can implement several hosts.
// Message thread only, except the play-state and transport atomics, which the audio thread writes.
namespace mu_clid
{

class SampleLibrary;

class RhythmHost
{
public:
    virtual ~RhythmHost() = default;

    // The parameter tree the layer's r{N}_ and ch{N}_ rows live in.
    virtual juce::AudioProcessorValueTreeState& getRhythmParameters() noexcept = 0;
    // The live rhythm count and rhythm `layer` (its generators, voice parameters, modulation matrix, name, colour).
    virtual int           getRhythmCount() const noexcept = 0;
    virtual Rhythm&       getRhythmLayer(int layer) noexcept = 0;
    virtual const Rhythm& getRhythmLayer(int layer) const noexcept = 0;

    // The transport as the content shows it, and the flag itself for the circles' own animation.
    virtual bool                     isRhythmTransportPlaying() const noexcept = 0;
    virtual const std::atomic<bool>& getRhythmTransportFlag() const noexcept = 0;
    // Layer `layer`'s play state (step, hit count), written by the audio thread.
    virtual RhythmPlayState&         getRhythmPlayState(int layer) noexcept = 0;
    // Post-matrix value of live-arc snapshot `snap` (ModulationSnapshot.h) on layer `layer`, in the knob's units.
    virtual float getRhythmModLive(int layer, int snap) const noexcept = 0;
    // The Euclid generators' values after modulation (the base values when nothing is assigned).
    virtual EuclidOverrides getRhythmEuclidOverrides(int layer) const noexcept = 0;
    // The layer's insert gain-reduction meter, or null when it has no engine.
    virtual const std::atomic<float>* getRhythmInsertGainReduction(int layer) = 0;

    // Bulk parameter writes: while the flag is set the parameter listeners skip their per-parameter refresh;
    // after one, the rhythm and its engine are re-read from the parameters.
    virtual bool               isRhythmParamsLoading() const noexcept = 0;
    virtual std::atomic<bool>& getRhythmParamsLoadingFlag() noexcept = 0;
    virtual void               syncRhythmFromParameters(int layer) = 0;

    // Layer edits the content's header performs: rename (under the layer lock), reset to defaults, and the
    // clip count the reset prompt names.
    virtual void renameRhythmLayer(int layer, const juce::String& newName) = 0;
    virtual void resetRhythmLayer(int layer) = 0;
    virtual int  getRhythmClipCount(int layer) const = 0;

    // Rhythm presets: the folder, saving one (optionally with the sample embedded), loading one (staged for the
    // layer's boundary while playing, applied at once when stopped) and the shared category list.
    virtual juce::File        getRhythmPresetsDir() const = 0;
    virtual void              saveRhythmPresetFile(int layer, const juce::File& dest, bool embedSample,
                                                   const juce::String& category, const juce::String& description) = 0;
    virtual void              stageRhythmPresetFile(int layer, const juce::File& file) = 0;
    virtual juce::StringArray getRhythmPresetCategories() const = 0;
    virtual void              addRhythmPresetCategory(const juce::String& category) = 0;

    // Samples: the per-layer paths, loading, preview and the primary folder, and the content folder's samples.
    virtual SampleLibrary& getRhythmSamples() noexcept = 0;
    virtual juce::File     getRhythmSamplesDir() const = 0;
};

} // namespace mu_clid
