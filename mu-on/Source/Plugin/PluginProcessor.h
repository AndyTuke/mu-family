#pragma once

#include "Plugin/ProcessorBase.h"        // mu-core base
#include "Plugin/MixerFxParams.h"         // mu-core: shared global-FX / mixer APVTS layout
#include "Sequencer/StepPattern.h"
#include "Sequencer/GrooveSequencer.h"

#include <array>
#include <atomic>

// mu-On — groove sequencer.
//
// Four FIXED instrument channels — Kick (synth), Bass (deep synth), Hat (sample),
// Snare (sample) — sequenced by a 909-style step grid. This is the ProcessorBase
// subclass that brings the shared platform online (APVTS mixer/FX layout, MixerEngine
// + FXChain, the shared sidebar + MixerOverlay). The bass↔kick side-chain ducking is
// the shared MixerEngine's channel-to-channel sidechain (wired by default in the ctor).
//
// Scaffold stage: processBlock routes a SILENT per-channel render through the shared
// mixer so the strips + VU meters are live; the sequencer + engines land next.
namespace mu_on
{

// Fixed channel layout — the four 909 instrument lanes.
enum Channel { Kick = 0, Bass = 1, Hat = 2, Snare = 3, kNumChannels = 4 };

class PluginProcessor : public ProcessorBase,
                        public juce::AudioProcessorValueTreeState::Listener
{
public:
    // Family parity: the shared mixer/sidebar size to kMaxChannels; mu-On uses a fixed 4.
    static constexpr int kMaxChannels = 8;

    PluginProcessor();
    ~PluginProcessor() override;

    // Mixer / FX params (channel strips + global FX) drive mixerEngine + fxChain via
    // the shared ProcessorBase::syncGlobalFxParam, kept in sync by this listener.
    void parameterChanged(const juce::String& id, float v) override;

    // ── AudioProcessor ───────────────────────────────────────────────────────
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;

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

    // ── Internal transport (TransportBar play/BPM) ────────────────────────────
    // Free-running clock; the sequencer reads the beat to advance its steps.
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
        switch (idx) { case Kick: return "Kick"; case Bass: return "Bass";
                       case Hat: return "Hat";   case Snare: return "Snare"; default: return {}; }
    }
    // Distinct palette entries per instrument lane.
    int          getChannelColourIndex(int idx) const override
    {
        static constexpr int kColours[kNumChannels] = { 0, 2, 5, 7 };  // Kick/Bass/Hat/Snare
        return (idx >= 0 && idx < kNumChannels) ? kColours[idx] : 0;
    }

    // ── 909 sequencer access (for the editor's grid + playhead) ───────────────
    StepPattern& pattern() noexcept { return stepPattern; }
    // Per-channel trigger counter — bumped on the audio thread when the sequencer fires
    // that lane; the editor polls it to pulse the sidebar lane. Read-only for the editor.
    int triggerCount(int ch) const noexcept
    {
        return (ch >= 0 && ch < kNumChannels) ? triggers[(size_t) ch].load(std::memory_order_relaxed) : 0;
    }

    // ── Preset directories / extensions (per family file-format rule) ─────────
    // Full = .muOn; per-channel ("track" = one instrument lane) = .muTrack.
    juce::File   getContentDir()             const override;
    juce::File   getPresetsDir()             const override;
    juce::File   getPerSlotPresetDir()       const override;
    juce::String getPerSlotPresetExtension() const override { return "muTrack"; }
    juce::File   getFullPresetDir()          const override { return getPresetsDir(); }
    juce::String getFullPresetExtension()    const override { return "muOn"; }

protected:
    // No MIDI-PC preset loading yet.
    void applyMidiPresetSlot(int, const juce::File&) override {}
    void applyFullMidiPreset(const juce::File&)      override {}

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void registerFxListeners();
    void syncAllFxParams();

    // Silent per-channel render hook handed to the shared MixerEngine (engines land next).
    MixerEngine::RenderChannelFn renderChannelCb;

    // 909 sequencer — owns the grid pattern; clocked off the internal transport each block.
    StepPattern     stepPattern;
    GrooveSequencer sequencer { stepPattern };
    std::array<std::atomic<int>, kNumChannels> triggers { };   // per-lane trigger counter (UI pulse)

    // Cached APVTS pointers for the product sequencer params (read each block).
    std::atomic<float>* seqSwingParam  = nullptr;
    std::atomic<float>* seqAccentParam = nullptr;

    std::atomic<bool>   playing { false };
    std::atomic<double> internalBeatPos { 0.0 };
    std::atomic<double> internalBpm { 120.0 };
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace mu_on
