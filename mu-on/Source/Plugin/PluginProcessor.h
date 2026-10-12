#pragma once

#include "Plugin/ProcessorBase.h"        // mu-core base
#include "Plugin/MixerFxParams.h"         // mu-core: shared global-FX / mixer APVTS layout
#include "Plugin/MidiClockSync.h"         // mu-core: shared MIDI-clock slave
#include "Plugin/TrackParams.h"
#include "Sequencer/StepPattern.h"
#include "Sequencer/GrooveSequencer.h"
#include "Audio/GrooveVoices.h"
#include "Sequencer/Track.h"

#include <array>
#include <atomic>
#include <memory>

// mu-On — groove sequencer.
//
// One to eight tracks (layers), each running one of five engines: Kick (synth), Bass (deep synth),
// Hat and Snare (samples), or Rumble (a techno "rumble" that processes another track's audio rather
// than stepping). The tracks are sequenced by a 909-style step grid, one row of 16 steps per track.
// This is the ProcessorBase subclass that brings the shared platform online (APVTS mixer/FX layout,
// MixerEngine + FXChain, the shared sidebar + MixerOverlay). All eight tracks' parameters and objects
// exist for the processor's lifetime; only the count (`numLayers`) is runtime, and add / delete /
// reorder go through the shared ProcessorBase structural edits. Bass-ducks-kick is the shared
// MixerEngine's channel-to-channel sidechain, set up in the mixer.
//
// processBlock clocks the 909 step sequencer and renders the engines through the shared mixer
// (engine → insert → mixer), so the strips + VU meters are live. Each track's engine params are
// resolved through its ModulationMatrix before rendering.
namespace mu_on
{

class PluginProcessor : public ProcessorBase
{
public:

    PluginProcessor();
    ~PluginProcessor() override;

    // Mixer / FX params (channel strips + global FX) drive mixerEngine + fxChain via
    // the shared ProcessorBase::syncGlobalFxParam, kept in sync by this listener.

    // ── AudioProcessor ───────────────────────────────────────────────────────
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override { resetSwapPlayState(); }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return juce::String (juce::CharPointer_UTF8 ("\xce\xbc-On")); }
    bool acceptsMidi()  const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ── ProcessorBase channel metadata (drives sidebar + mixer) ───────────────
    // getNumChannels / getLayer are the base defaults over the owned layer storage (the count is
    // `numLayers`, 1..kMaxLayers).
    // A track is named after the engine it runs; the name is not saved, it follows the engine.
    juce::String getChannelName(int idx)       const override
    {
        return (idx >= 0 && idx < mu_limits::kMaxLayers) ? juce::String(engineName(engineOf(idx))) : juce::String();
    }
    // The palette colour is allocated per track and follows it through delete / reorder.
    int          getChannelColourIndex(int idx) const override
    {
        return (idx >= 0 && idx < mu_limits::kMaxLayers) ? layers[(size_t) idx]->colourIndex : 0;
    }

    // Add / delete / reorder are ProcessorBase::addLayer / removeLayer / swapLayers; this product
    // supplies createLayer, layersMoved and layerAdded. The edits move the Track pointers under
    // `layersLock`, which processBlock tryLocks.
    bool canEditLayers() const override { return true; }
    // The engines and the step-row binding of a slot stay put while its Track moves, so they are
    // rebound / cleared for the new tenant (called under layersLock; allocation-free).
    void layersMoved(int firstIndex, int lastIndex) override;
    // A new track gets the first engine no live track uses (else Kick) and that engine's groove.
    void layerAdded(int index) override;

    // ── The tracks ───────────────────────────────────────────────────────────
    // Layer `i` as its mu-On Track (colour, modulation, step row, Rumble envelope). The layers live in
    // ProcessorCore; the editor's ModulatorPanel binds to the shown one through this.
    Track&       getTrack(int i) noexcept       { return layerAs<Track>(juce::jlimit(0, mu_limits::kMaxLayers - 1, i)); }
    const Track& getTrack(int i) const noexcept { return layerAs<Track>(juce::jlimit(0, mu_limits::kMaxLayers - 1, i)); }

    // The engine track `i` runs (its `t{N}_eng` parameter).
    EngineType engineOf(int i) const noexcept
    {
        const auto* p = (i >= 0 && i < mu_limits::kMaxLayers) ? engineParam[(size_t) i] : nullptr;
        return engineFromIndex(p != nullptr ? juce::roundToInt(p->load(std::memory_order_relaxed)) : 0);
    }

    // Switch track `i` to `engine`: every engine keeps its settings on the track, the step row and
    // envelope stay, and the track's modulation assignments that target another engine's parameters
    // are dropped. Message thread.
    void selectEngine(int i, EngineType engine);

    // The first live track running Kick, or 0 when there is none (a Rumble's default source).
    int firstKickTrack() const noexcept;

    // 909 sequencer access (for the editor's grid + playhead): row `i` is track `i`'s steps.
    StepPattern& pattern() noexcept { return stepPattern; }

    // Per-track trigger counter — bumped on the audio thread when the sequencer fires that track;
    // the editor polls it to pulse the sidebar item. Read-only for the editor.
    int triggerCount(int i) const noexcept
    {
        return (i >= 0 && i < mu_limits::kMaxLayers) ? getTrack(i).triggers.load(std::memory_order_relaxed) : 0;
    }

    // ── Preset directories / extensions (per family file-format rule) ─────────
    // Full = .muOn; per-track ("track" = one layer) = .muTrack.
    juce::File   getLayerPresetDir()       const override;
    juce::String getLayerPresetExtension() const override { return "muTrack"; }
    juce::String getFullPresetExtension()    const override { return "muOn"; }

    // Full-preset save/load — the editor shell drives the UI (preset bar, Save dialog, browser).
    // A preset is the params + every track's step row, modulators and Rumble envelope, wrapped with
    // name / description / category.
    // Full presets: the standard ProcessorBase save / load / categories, under <MuOnPreset>.
    const char*     getFullPresetTag() const override { return "MuOnPreset"; }
    juce::ValueTree captureFullPreset() override { return captureState(); }
    void            useLoadedFullPreset(juce::ValueTree state) override;

    // Per-track presets — a track's engine and all its engines' params, its step row, Rumble envelope
    // and modulators; loadable into any track. (ProcessorBase layer API.)
    void                    saveLayerPreset(int track, const juce::String& name) override;
    void                    loadLayerPreset(int track, const juce::File& file) override;
    juce::Array<juce::File> slotPresetFiles(int track) const override;
    void                    resetLayer(int track) override;   // the engine's params → defaults, modulators cleared; the engine, source, steps and envelope stay

    // Hot-swap: a preset loaded while the transport runs is staged and applied when the 16-step
    // pattern wraps (every bar, or at once when playback stops); loaded while stopped, at once.
    // (The staging itself is ProcessorBase::layerSwapper.)

protected:
    // MIDI program change (drained on the message thread): Ch 1-8 → that track's preset,
    // Ch 9 → full preset, each through the same hot-swap path as a load from the UI.
    void applyFullMidiPreset(const juce::File& f) override
    {
        loadPreset(f);
        if (! layerSwapper.hasFullPending() && onPresetSwapCommitted) onPresetSwapCommitted();
    }

    // A default Track for slot `slot`, built before a structural edit takes the lock.
    std::unique_ptr<Layer> createLayer(int slot) override;

    // A Rumble track's source is a layer index: it follows its layer through delete / reorder, and a
    // deleted source becomes the first Kick.
    void remapLayerReferences(const std::function<int(int)>& oldToNew) override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // State shared by the session save/restore and full presets (PluginProcessor_Preset.cpp):
    // the composed state (mu-core LayerState) of the globals + every track's node.
    void                setupLayerState();
    juce::ValueTree     captureState() { return captureComposedState(); }
    juce::ValueTree     toLayerState(const juce::ValueTree& tree) const;
    void                applyStateTree(const juce::ValueTree& state);

    // Per-channel render hook handed to the shared MixerEngine — copies each track's block, rendered by
    // GrooveVoices::renderAll before the mixer runs.
    MixerEngine::RenderChannelFn renderChannelCb;

    // 909 sequencer — owns the grid pattern; clocked off the internal transport each block.
    StepPattern     stepPattern;
    GrooveSequencer sequencer { stepPattern };
    std::unique_ptr<GrooveVoices> grooveVoicesStore = std::make_unique<GrooveVoices>();   // every track slot's engines (large: kept off the stack)
    GrooveVoices&   grooveVoices = *grooveVoicesStore;

    // Cached APVTS pointers for the product sequencer params and each track's engine choice.
    std::atomic<float>* seqSwingParam  = nullptr;
    std::atomic<float>* seqAccentParam = nullptr;
    std::array<std::atomic<float>*, mu_limits::kMaxLayers> engineParam {};

    double currentSampleRate = 44100.0;
    int    currentBlockSize  = 512;
    bool   wasPlaying = false;   // audio-thread only — detects the play→stop edge to silence voices

    // Hot-swap: parsed preset trees per track + one full preset, committed at the pattern wrap.
    void applyTrackTree(int track, const juce::ValueTree& tree);

    // A Rumble track whose source is itself, gone, or another Rumble starts on the first Kick.
    void ensureRumbleSource(int track);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace mu_on
