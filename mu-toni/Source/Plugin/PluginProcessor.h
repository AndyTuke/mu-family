#pragma once

#include "Plugin/ProcessorBase.h"        // mu-core base
#include "Plugin/LayerParamIds.h"        // layerParamPrefix / layerParamId
#include "Plugin/MixerFxParams.h"         // mu-core: shared global-FX / mixer APVTS layout
#include "Plugin/MidiClockSync.h"         // mu-core: shared MIDI-clock slave
#include "Sequencer/Arp.h"                // mu-Toni layer: Layer + arp voice + insert
#include "Modulation/LaneModulation.h"    // mu-core: mu_mod::resolveLane
#include "Modulation/MuToniModDest.h"     // arp/voice modulation destinations

#include <array>
#include <atomic>
#include <memory>
#include <string_view>
#include <unordered_map>

// mu-Toni — generative arpeggiator mono-synth.
//
// The ProcessorBase subclass owning the arp + voice engine and the shared
// platform: the APVTS mixer/FX layout, the MixerEngine + FXChain, and 1..8 "layer"
// channels for the shared sidebar + MixerOverlay. Each layer runs an independent mono arp
// (ArpVoiceRunner → ToniVoice), rendered through the shared mixer via the processCoreBlock hook
// (engine→insert→mixer path). All eight layers' parameters and objects exist for the processor's
// lifetime; only the count (`numLayers`) is runtime, and add / delete / reorder go through the
// shared ProcessorBase structural edits.
namespace mu_toni
{

class PluginProcessor : public ProcessorBase
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    // Mixer / FX params (channel strips + global FX) drive mixerEngine + fxChain
    // via the shared ProcessorBase::syncGlobalFxParam, kept in sync by this listener.

    // ── AudioProcessor ───────────────────────────────────────────────────────
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override { resetSwapPlayState(); }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return juce::String (juce::CharPointer_UTF8 ("\xce\xbc-Toni")); }
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
    // getNumLayers / getLayer are the base defaults over the owned layer storage (the count is
    // `numLayers`, 1..kMaxLayers).
    juce::String getLayerName(int idx)       const override
    {
        return (idx >= 0 && idx < mu_limits::kMaxLayers) ? "Layer " + juce::String(idx + 1) : juce::String();
    }
    // The palette colour is allocated per layer and follows it through delete / reorder.
    int          getLayerColourIndex(int idx) const override
    {
        return (idx >= 0 && idx < mu_limits::kMaxLayers) ? layers[(size_t) idx]->colourIndex : 0;
    }

    // Add / delete / reorder / reset are ProcessorBase::addLayer / removeLayer / swapLayers /
    // resetLayer; this product supplies createLayer and layersMoved. The edits move the Arp
    // pointers under `layersLock`, which processBlock tryLocks.
    bool canEditLayers() const override { return true; }
    // The audio state of a slot stays put while its Arp moves, so it is cleared for the new tenant
    // (called under layersLock; allocation-free).
    void layersMoved(int firstIndex, int lastIndex) override;

    // ── Presets (per family file-format rule) ─────────────────────────────────
    // Full = .muToni (the whole state); per-layer = .muArp (one layer's arp + voice).
    juce::File   getLayerPresetDir()       const override;
    juce::String getLayerPresetExtension() const override { return "muArp"; }
    juce::String getFullPresetExtension()    const override { return "muToni"; }

    // Full-preset save/load — the editor shell drives the UI (preset bar, Save dialog, browser).
    // A preset is the params + every layer's modulators, wrapped with name / description / category.
    // Full presets: the standard ProcessorBase save / load / categories, under <MuToniPreset>.
    const char*     getFullPresetTag() const override { return "MuToniPreset"; }
    juce::ValueTree captureFullPreset() override { return captureState(); }
    void            useLoadedFullPreset(juce::ValueTree state) override;

    // Per-layer presets (ProcessorBase slot API) — the layer's a{N}_ params + its modulators
    // (loadable into any layer), and a reset back to defaults.
    void saveLayerPreset(int layer, const juce::String& name) override;
    void loadLayerPreset(int layer, const juce::File& file) override;

    juce::ValueTree captureLayerPresetTree(int layer) override;
protected:
    void stageLayerPresetTree(int layer, juce::ValueTree tree, bool keepIdentity) override;
public:

    // Hot-swap: a preset loaded while the transport runs is staged and applied at the next bar
    // line (or at once when playback stops); loaded while stopped, it applies at once.
    // (The staging itself is ProcessorBase::layerSwapper.)

protected:
    // MIDI program change (drained on the message thread): Ch 1-8 → that layer's preset,
    // Ch 9 → full preset, each through the same hot-swap path as a load from the UI.
    void applyFullMidiPreset(const juce::File& f) override
    {
        loadPreset(f);
        if (! layerSwapper.hasFullPending() && onPresetSwapCommitted) onPresetSwapCommitted();
    }

    // A default Arp for slot `slot`, built and prepared before a structural edit takes the lock.
    std::unique_ptr<Layer> createLayer(int slot) override;

    // ── Arp/voice parameter cache (index into vp[voice][slot]) ────────────────
    // Enum + suffix table live in the .cpp; count is needed here for the array.
    static constexpr int kNumVoiceParams = 63;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // State shared by the session save/restore and full presets (PluginProcessor_Preset.cpp):
    // the composed state (mu-core LayerState) of the globals + every layer's node.
    void            setupLayerState();
    juce::ValueTree captureState() { return captureComposedState(); }
    juce::ValueTree toLayerState(const juce::ValueTree& tree) const;
    void            applyStateTree(const juce::ValueTree& state);

    // Register mixer/FX param listeners + run an initial engine sync (JUCE doesn't
    // fire parameterChanged on construction or for unchanged values).

    // Cache the per-voice arp/voice/env raw parameter pointers (message thread).
    void cacheVoiceParamPointers();
    // Resolve a voice's modulation (matrix over its control sequences) then build its
    // ArpParams + ToniVoiceParams + step config. Non-const: runs the matrix under try-lock.
    void readVoice(int v, ArpParams& ap, ToniVoiceParams& vp, int& rateIdx, float& gate01, bool& midiTrig,
                   ArpAccent& accent);
    // Update the held-note stack from incoming MIDI; sets noteOnEdge if a new note landed.
    void updateHeldNotes(const juce::MidiBuffer& midi, bool& noteOnEdge);

    mu_wavetable::WavetableBank bank;                        // the oscillators' wavetables (factory set)
    std::atomic<float>*         midiInChParam = nullptr;     // MIDI In channel (0 = Omni)
    std::array<std::array<std::atomic<float>*, kNumVoiceParams>, mu_limits::kMaxLayers>    vp {};

public:
    // Layer `i` as its mu-Toni Arp (name, colour, modulation, arp voice, insert effect). The layers
    // live in ProcessorCore; the UI ModulatorPanel binds to the active one through this.
    Arp&       getArp(int i) noexcept       { return layerAs<Arp>(i); }
    const Arp& getArp(int i) const noexcept { return layerAs<Arp>(i); }
    // Post-matrix value of modulation target `dest` (a ModDestIndex) on layer `layer`, in the
    // target knob's parameter units. Written each block by readVoice; read by the knobs' live arcs.
    float getModLive(int layer, int dest) const noexcept
    {
        return modLive[(size_t) juce::jlimit(0, mu_limits::kMaxLayers - 1, layer)]
                      [(size_t) juce::jlimit(0, kNumModDests - 1, dest)].load(std::memory_order_relaxed);
    }
private:
    std::array<std::array<std::atomic<float>, kNumModDests>, mu_limits::kMaxLayers> modLive {};

    // Modulation-resolve inputs: parallel arrays for mu_mod::resolveLane.
    std::array<const char*, kNumModDests>                                         modDestIds {};
    std::array<juce::NormalisableRange<float>, kNumModDests>                       modDestRanges {};
    std::array<std::array<std::atomic<float>*, kNumModDests>, mu_limits::kMaxLayers>        modDestAtoms {};
    std::unordered_map<std::string_view, float>                                   modParamValues;
    double                                                                        modBeat = 0.0;

    // Root-by-MIDI held-note stack (newest on top) + per-block arp context.
    std::array<int, 32> heldStack {};
    int                 heldCount = 0;
    ArpContext          arpCtx;

    // Renders channel `ch` from its arp runner. Captures `this` (reads arpCtx).
    MixerEngine::RenderChannelFn renderChannelCb;


    // Hot-swap: parsed preset trees per layer + one full preset, committed at the bar line.
    void applyLayerTree(int layer, const juce::ValueTree& tree);
    // Written by prepareToPlay, read by createLayer on the message thread and by processBlock.
    std::atomic<double> currentSampleRate { 44100.0 };
    std::atomic<int>    currentBlockSize  { 512 };
    // Per-instance noise seed salt, taken before the constructor body builds the layers; 0 when MU_DETERMINISTIC_NOISE is set (golden renders).
    const juce::int64   noiseSeedSalt = mu_core::makeNoiseSalt();
    int                 runnersLive = 0;   // audio thread: layers whose runner has run since it was last reset

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace mu_toni
