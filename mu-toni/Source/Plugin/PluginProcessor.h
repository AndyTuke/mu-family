#pragma once

#include "Plugin/HotSwap.h"   // mu-core: preset hot-swap staging
#include "Plugin/ProcessorBase.h"        // mu-core base
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
// platform: the APVTS mixer/FX layout, the MixerEngine + FXChain, and a fixed set
// of "layer" channels for the shared sidebar + MixerOverlay. Each layer runs an
// independent mono arp (ArpVoiceRunner → ToniVoice), rendered through the shared
// mixer via the processCoreBlock hook (engine→insert→mixer path).
namespace mu_toni
{

class PluginProcessor : public ProcessorBase
{
public:
    // Family parity: up to 8 channels/layers. A fixed set ships for now; dynamic
    // add/delete/reorder is still unwired (no addVoice/removeVoice).
    static constexpr int kMaxChannels = mu_limits::kMaxLayers;   // the family layer cap
    static constexpr int kNumChannels = 4;   // placeholder layers shown in the shell

    PluginProcessor();

    // Mixer / FX params (channel strips + global FX) drive mixerEngine + fxChain
    // via the shared ProcessorBase::syncGlobalFxParam, kept in sync by this listener.

    // ── AudioProcessor ───────────────────────────────────────────────────────
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
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
    int          getNumChannels()              const override { return kNumChannels; }
    Layer*       getLayer(int i) override { return (i >= 0 && i < kNumChannels) ? &layers[(size_t) i] : nullptr; }
    using ProcessorBase::getLayer;
    int          getMaxChannels()              const override { return kNumChannels; }
    juce::String getChannelName(int idx)       const override
    {
        return (idx >= 0 && idx < kNumChannels) ? "Layer " + juce::String(idx + 1) : juce::String();
    }
    int          getChannelColourIndex(int idx) const override
    {
        return (idx >= 0 && idx < kMaxChannels) ? idx : 0;
    }

    // ── Presets (per family file-format rule) ─────────────────────────────────
    // Full = .muToni (the whole state); per-layer = .muArp (one layer's arp + voice).
    juce::File   getPerSlotPresetDir()       const override;
    juce::String getPerSlotPresetExtension() const override { return "muArp"; }
    juce::String getFullPresetExtension()    const override { return "muToni"; }

    // Full-preset save/load — the editor shell drives the UI (preset bar, Save dialog, browser).
    // A preset is the params + every layer's modulators, wrapped with name / description / category.
    // Full presets: the standard ProcessorBase save / load / categories, under <MuToniPreset>.
    const char*     getFullPresetTag() const override { return "MuToniPreset"; }
    juce::ValueTree captureFullPreset() override { return captureState(); }
    void            useLoadedFullPreset(juce::ValueTree state) override;

    // Per-layer presets (ProcessorBase slot API) — the layer's v{N}_ params + its modulators
    // (loadable into any layer), and a reset back to defaults.
    void saveSlotPreset(int layer, const juce::String& name) override;
    void loadSlotPreset(int layer, const juce::File& file) override;
    void resetSlot(int layer) override;

    // Hot-swap: a preset loaded while the transport runs is staged and applied at the next bar
    // line (or at once when playback stops); loaded while stopped, it applies at once.
    bool hasPendingFullPreset() const override  { return hotSwap.hasFullPending(); }
    bool hasPendingSwap(int layer) const override { return hotSwap.hasPending(layer); }

protected:
    // MIDI program change (drained on the message thread): Ch 1-4 → that layer's preset,
    // Ch 9 → full preset, each through the same hot-swap path as a load from the UI.
    void applyFullMidiPreset(const juce::File& f) override
    {
        loadPreset(f);
        if (! hotSwap.hasFullPending() && onPresetSwapCommitted) onPresetSwapCommitted();
    }
    // Commit the staged swaps that reached their bar line (message thread).
    void commitDeferredWork() override;

    // ── Arp/voice parameter cache (index into vp[voice][slot]) ────────────────
    // Enum + suffix table live in the .cpp; count is needed here for the array.
    static constexpr int kNumVoiceParams = 62;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // State shared by the session save/restore and full presets (PluginProcessor_Preset.cpp):
    // the composed state (mu-core LayerState) of the globals + every layer's node.
    void            initLayerState();
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
    std::array<std::array<std::atomic<float>*, kNumVoiceParams>, kMaxChannels>    vp {};

    // The layers: each is a mu-core Layer (name, colour, modulation) with its arp voice and insert
    // effect. Public so the UI ModulatorPanel can bind to the active layer.
public:
    std::array<Arp, kMaxChannels> layers;
private:
    // Modulation-resolve inputs: parallel arrays for mu_mod::resolveLane.
    std::array<const char*, kNumModDests>                                         modDestIds {};
    std::array<juce::NormalisableRange<float>, kNumModDests>                       modDestRanges {};
    std::array<std::array<std::atomic<float>*, kNumModDests>, kMaxChannels>        modDestAtoms {};
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
    mu_hotswap::BarLineSwapper<juce::ValueTree, kNumChannels> hotSwap;
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace mu_toni
