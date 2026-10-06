#pragma once

#include "Plugin/ProcessorBase.h"        // mu-core base
#include "Plugin/MixerFxParams.h"         // mu-core: shared global-FX / mixer APVTS layout
#include "Plugin/MidiClockSync.h"         // mu-core: shared MIDI-clock slave
#include "Sequencer/ArpVoiceRunner.h"     // per-voice arpeggiator + ToniVoice
#include "Sequencer/VoiceSlot.h"          // mu-core: per-voice control sequences + matrix
#include "Modulation/LaneModulation.h"    // mu-core: mu_mod::resolveLane
#include "Modulation/MuToniModDest.h"     // arp/voice modulation destinations
#include "Audio/InsertProcessor.h"        // mu-core: shared per-voice insert FX

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

class PluginProcessor : public ProcessorBase,
                        public juce::AudioProcessorValueTreeState::Listener,
                        private juce::AsyncUpdater
{
public:
    // Family parity: up to 8 channels/layers. A fixed set ships for now; dynamic
    // add/delete/reorder is still unwired (no addVoice/removeVoice).
    static constexpr int kMaxChannels = 8;
    static constexpr int kNumChannels = 4;   // placeholder layers shown in the shell

    PluginProcessor();
    ~PluginProcessor() override;

    // Mixer / FX params (channel strips + global FX) drive mixerEngine + fxChain
    // via the shared ProcessorBase::syncGlobalFxParam, kept in sync by this listener.
    void parameterChanged(const juce::String& id, float v) override;

    // ── AudioProcessor ───────────────────────────────────────────────────────
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;

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

    // ── Internal transport (TransportBar play/BPM) ────────────────────────────
    // Free-running clock so the shell's play button + BPM box are live. Nothing
    // consumes the beat yet — the engine/sequencer will.
    bool   isInternalPlaying()  const override { return playing.load(std::memory_order_relaxed); }
    void   toggleInternalPlay() override
    {
        const bool now = !playing.load(std::memory_order_relaxed);
        playing.store(now, std::memory_order_relaxed);
        if (!now) internalBeatPos.store(0.0, std::memory_order_relaxed);
    }
    double getInternalBpm()     const override { return internalBpm.load(std::memory_order_relaxed); }
    void   setInternalBpm(double bpm) override { internalBpm.store(juce::jlimit(20.0, 300.0, bpm), std::memory_order_relaxed); }
    double getInternalBeatPos() const override { return internalBeatPos.load(std::memory_order_relaxed); }

    // ── ProcessorBase channel metadata (drives sidebar + mixer) ───────────────
    int          getNumChannels()              const override { return kNumChannels; }
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
    juce::File   getPresetsDir()             const override;
    juce::File   getPerSlotPresetDir()       const override;
    juce::String getPerSlotPresetExtension() const override { return "muArp"; }
    juce::File   getFullPresetDir()          const override { return getPresetsDir(); }
    juce::String getFullPresetExtension()    const override { return "muToni"; }

    // Full-preset save/load — the editor shell drives the UI (preset bar, Save dialog, browser).
    // A preset is the params + every layer's modulators, wrapped with name / description / category.
    void              savePreset(const juce::String& name, const juce::String& desc,
                                 const juce::String& category, bool embedSamples) override;
    void              loadPreset(const juce::File& file) override;
    juce::StringArray loadCategoryList() const override;

    // Per-layer presets — the layer's v{N}_ params + its modulators (loadable into any layer),
    // and a reset back to defaults.
    void saveLayerPreset(int layer, const juce::String& name);
    void loadLayerPreset(int layer, const juce::File& file);
    void resetLayer(int layer);

    // Fired (message thread) after a program change loads a layer preset, so the editor can
    // refresh that layer. The editor MUST clear this in its destructor.
    std::function<void(int layer)> onLayerPresetLoaded;

protected:
    // MIDI program change (drained on the message thread): Ch 1-4 → that layer's preset,
    // Ch 9 → full preset. Applied immediately, then the editor is told to refresh.
    void applyMidiPresetSlot(int slot, const juce::File& f) override
    {
        loadLayerPreset(slot, f);
        if (onLayerPresetLoaded) onLayerPresetLoaded(slot);
    }
    void applyFullMidiPreset(const juce::File& f) override
    {
        loadPreset(f);
        if (onPresetSwapCommitted) onPresetSwapCommitted();
    }

    // ── Arp/voice parameter cache (index into vp[voice][slot]) ────────────────
    // Enum + suffix table live in the .cpp; count is needed here for the array.
    static constexpr int kNumVoiceParams = 51;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // State shared by the session save/restore and full presets (PluginProcessor_Preset.cpp).
    juce::ValueTree captureState();
    void            applyStateTree(juce::ValueTree tree);
    void            handleAsyncUpdate() override;   // drains queued program changes

    // Register mixer/FX param listeners + run an initial engine sync (JUCE doesn't
    // fire parameterChanged on construction or for unchanged values).

    // Cache the per-voice arp/voice/env raw parameter pointers (message thread).
    void cacheVoiceParamPointers();
    // Resolve a voice's modulation (matrix over its control sequences) then build its
    // ArpParams + ToniVoiceParams + step config. Non-const: runs the matrix under try-lock.
    void readVoice(int v, ArpParams& ap, ToniVoiceParams& vp, int& rateIdx, float& gate01, bool& midiTrig);
    // Update the held-note stack from incoming MIDI; sets noteOnEdge if a new note landed.
    void updateHeldNotes(const juce::MidiBuffer& midi, bool& noteOnEdge);

    // Per-channel arp voice + its cached parameter pointers.
    std::array<ArpVoiceRunner, kMaxChannels>                                      runners;
    std::array<std::array<std::atomic<float>*, kNumVoiceParams>, kMaxChannels>    vp {};

    // Per-channel insert effect (shared mu-core InsertProcessor), applied post-VCA
    // in the render callback. insCfg holds each voice's algo + 4 slot params, set
    // per block by readVoice.
    std::array<InsertProcessor, kMaxChannels> inserts;
    std::array<VoiceParams,      kMaxChannels> insCfg;

    // ── Per-voice modulation (mu-core VoiceSlot + shared matrix) ───────────────
    // Public so the UI ModulatorPanel can bind to the active voice's slot.
public:
    std::array<VoiceSlot, kMaxChannels> voiceSlots;
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

    std::atomic<bool>   playing { false };
    std::atomic<double> internalBeatPos { 0.0 };
    std::atomic<double> internalBpm { 120.0 };
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace mu_toni
