#pragma once

#include "Plugin/ProcessorBase.h"        // mu-core base
#include "Plugin/MixerFxParams.h"         // mu-core: shared global-FX / mixer APVTS layout

#include <atomic>

// mu-Toni — scaffold.
//
// The product-specific synth engine + sequencer are NOT yet defined. This is the
// minimal ProcessorBase subclass that brings the whole shared platform online:
// the APVTS mixer/FX layout, the MixerEngine + FXChain, and a fixed set of
// "layer" channels for the shared sidebar + MixerOverlay. processBlock routes a
// silent render through the shared mixer (engine→insert→mixer path) so the mixer
// is genuinely wired — there's just no sound source until the engine lands.
namespace mu_toni
{

class PluginProcessor : public ProcessorBase,
                        public juce::AudioProcessorValueTreeState::Listener
{
public:
    // Family parity: up to 8 channels/layers. The scaffold ships a fixed set;
    // dynamic add/delete arrives with the engine.
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

    // ── Preset directories / extensions (per family file-format rule) ─────────
    // Save/load themselves stay on ProcessorBase's no-op defaults for now (preset
    // chrome is disabled in the editor); these satisfy the pure-virtuals + give
    // the future preset I/O its home. Full = .muToni; per-layer = .muLayer (a
    // neutral placeholder noun until the engine names its layer concept).
    juce::File   getContentDir()             const override;
    juce::File   getPresetsDir()             const override;
    juce::File   getPerSlotPresetDir()       const override;
    juce::String getPerSlotPresetExtension() const override { return "muLayer"; }
    juce::File   getFullPresetDir()          const override { return getPresetsDir(); }
    juce::String getFullPresetExtension()    const override { return "muToni"; }

protected:
    // No MIDI-PC preset loading yet.
    void applyMidiPresetSlot(int, const juce::File&) override {}
    void applyFullMidiPreset(const juce::File&)      override {}

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Register mixer/FX param listeners + run an initial engine sync (JUCE doesn't
    // fire parameterChanged on construction or for unchanged values).
    void registerFxListeners();
    void syncAllFxParams();

    // Silent per-channel render hook handed to the shared MixerEngine — clears the
    // channel buffer (no engine yet). Captures only `this`.
    MixerEngine::RenderChannelFn renderChannelCb;

    std::atomic<bool>   playing { false };
    std::atomic<double> internalBeatPos { 0.0 };
    std::atomic<double> internalBpm { 120.0 };
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace mu_toni
