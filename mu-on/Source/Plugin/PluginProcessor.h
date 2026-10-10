#pragma once

#include "Plugin/HotSwap.h"   // mu-core: preset hot-swap staging
#include "Plugin/ProcessorBase.h"        // mu-core base
#include "Plugin/MixerFxParams.h"         // mu-core: shared global-FX / mixer APVTS layout
#include "Plugin/MidiClockSync.h"         // mu-core: shared MIDI-clock slave
#include "Plugin/MuOnChannels.h"
#include "Sequencer/StepPattern.h"
#include "Sequencer/GrooveSequencer.h"
#include "Audio/GrooveVoices.h"
#include "Sequencer/Layer.h"   // mu-core: per-lane modulation slot

#include <array>
#include <atomic>
#include <memory>

// mu-On — groove sequencer.
//
// Four FIXED instrument channels — Kick (synth), Bass (deep synth), Hat (sample),
// Snare (sample) — sequenced by a 909-style step grid. This is the ProcessorBase
// subclass that brings the shared platform online (APVTS mixer/FX layout, MixerEngine
// + FXChain, the shared sidebar + MixerOverlay). The bass↔kick side-chain ducking is
// the shared MixerEngine's channel-to-channel sidechain (wired by default in the ctor).
//
// processBlock clocks the 909 step sequencer and renders the four engines (Kick/Bass/
// Hat/Snare) through the shared mixer (engine → insert → mixer), so the strips + VU
// meters are live and the lanes are audible. Each lane's engine params are resolved
// through its ModulationMatrix before rendering.
namespace mu_on
{

class PluginProcessor : public ProcessorBase
{
public:
    // Family parity: the shared mixer/sidebar size to kMaxChannels; mu-On uses a fixed 4.
    static constexpr int kMaxChannels = mu_limits::kMaxLayers;   // the family layer cap

    PluginProcessor();

    // Mixer / FX params (channel strips + global FX) drive mixerEngine + fxChain via
    // the shared ProcessorBase::syncGlobalFxParam, kept in sync by this listener.

    // ── AudioProcessor ───────────────────────────────────────────────────────
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
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
    int          getNumChannels()              const override { return kNumChannels; }
    int          getMaxChannels()              const override { return kNumChannels; }
    juce::String getChannelName(int idx)       const override
    {
        switch (idx) { case Kick: return "Kick"; case Bass: return "Bass";
                       case Hat: return "Hat";   case Snare: return "Snare";
                       case Rumble: return "Rumble"; default: return {}; }
    }
    // Distinct palette entries per instrument lane.
    int          getChannelColourIndex(int idx) const override
    {
        static constexpr int kColours[kNumChannels] = { 0, 2, 1, 3, 4 };  // Kick red / Bass orange / Hat cyan / Snare magenta / Rumble lime
        return (idx >= 0 && idx < kNumChannels) ? kColours[idx] : 0;
    }

    // ── 909 sequencer access (for the editor's grid + playhead) ───────────────
    StepPattern& pattern() noexcept { return stepPattern; }

    // Rumble lane's drawable bar-volume envelope (a smooth ControlSequence drawn in the grid
    // slot for the Rumble lane). Edited on the message thread under `rumbleEnvLock`; the audio
    // thread evaluates it under a try-lock each block. Persists in the state tree.
    ControlSequence&  rumbleEnvelope() noexcept { return rumbleEnv; }
    CopyableSpinLock& rumbleEnvLockRef() noexcept { return rumbleEnvLock; }

    // Per-lane modulation slot (ControlSequences + ModulationMatrix) — the editor's
    // shared ModulatorPanel binds to the selected lane's slot; the audio thread reads
    // it via GrooveVoices each block.
    Layer& voiceSlot(int lane) noexcept { return voiceSlots[(size_t) juce::jlimit(0, kNumChannels - 1, lane)]; }
    // Per-channel trigger counter — bumped on the audio thread when the sequencer fires
    // that lane; the editor polls it to pulse the sidebar lane. Read-only for the editor.
    int triggerCount(int ch) const noexcept
    {
        return (ch >= 0 && ch < kNumChannels) ? triggers[(size_t) ch].load(std::memory_order_relaxed) : 0;
    }

    // ── Preset directories / extensions (per family file-format rule) ─────────
    // Full = .muOn; per-channel ("track" = one instrument lane) = .muTrack.
    juce::File   getPerSlotPresetDir()       const override;
    juce::String getPerSlotPresetExtension() const override { return "muTrack"; }
    juce::String getFullPresetExtension()    const override { return "muOn"; }

    // Full-preset save/load — the editor shell drives the UI (preset bar, Save dialog, browser).
    // A preset is the params + step grid + each lane's modulators + the Rumble envelope,
    // wrapped with name / description / category.
    // Full presets: the standard ProcessorBase save / load / categories, under <MuOnPreset>.
    const char*     getFullPresetTag() const override { return "MuOnPreset"; }
    juce::ValueTree captureFullPreset() override { return captureState(); }
    void            useLoadedFullPreset(juce::ValueTree state) override;

    // Per-track presets — a lane's engine params, its step row (Rumble: its envelope) and its
    // modulators. A track preset belongs to the instrument it was saved from.
    // (ProcessorBase slot API.)
    void                    saveSlotPreset(int lane, const juce::String& name) override;
    void                    loadSlotPreset(int lane, const juce::File& file) override;
    juce::Array<juce::File> slotPresetFiles(int lane) const override;   // the presets for that lane
    void                    resetSlot(int lane) override;                // engine params → defaults, modulators cleared

    // Hot-swap: a preset loaded while the transport runs is staged and applied when the 16-step
    // pattern wraps (every bar, or at once when playback stops); loaded while stopped, at once.
    bool hasPendingFullPreset() const override   { return hotSwap.hasFullPending(); }
    bool hasPendingSwap(int lane) const override { return hotSwap.hasPending(lane); }

protected:
    // MIDI program change (drained on the message thread): Ch 1-5 → that lane's track preset,
    // Ch 9 → full preset, each through the same hot-swap path as a load from the UI.
    void applyFullMidiPreset(const juce::File& f) override
    {
        loadPreset(f);
        if (! hotSwap.hasFullPending() && onPresetSwapCommitted) onPresetSwapCommitted();
    }
    // Commit the staged swaps that reached the pattern wrap (message thread).
    void commitDeferredWork() override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // State shared by the session save/restore and full presets (PluginProcessor_Preset.cpp):
    // the composed state (mu-core LayerState) of the globals + every lane's node.
    void                initLaneState();
    juce::ValueTree     captureState() { return captureComposedState(); }
    juce::ValueTree     toLaneState(const juce::ValueTree& tree) const;
    void                applyStateTree(const juce::ValueTree& state);
    juce::ValueTree     serialiseRumbleEnv();
    void                restoreRumbleEnv(const juce::ValueTree& env);
    static juce::String lanePrefix(int lane);       // the lane's engine-param prefix (k_, b_, ...)

    // Per-channel render hook handed to the shared MixerEngine — fills each lane's buffer
    // from its engine (Kick/Bass/Hat/Snare) via GrooveVoices.
    MixerEngine::RenderChannelFn renderChannelCb;

    // 909 sequencer — owns the grid pattern; clocked off the internal transport each block.
    StepPattern     stepPattern;
    GrooveSequencer sequencer { stepPattern };
    GrooveVoices    grooveVoices;                              // the four instrument engines
    std::array<Layer, kNumChannels> voiceSlots;            // per-lane modulation (ControlSequences + matrix)
    ControlSequence  rumbleEnv;                                // Rumble lane drawable bar-volume envelope
    CopyableSpinLock rumbleEnvLock;                            // guards rumbleEnv (msg edit ↔ audio eval)
    std::array<std::atomic<int>, kNumChannels> triggers { };   // per-lane trigger counter (UI pulse)

    // Cached APVTS pointers for the product sequencer params (read each block).
    std::atomic<float>* seqSwingParam  = nullptr;
    std::atomic<float>* seqAccentParam = nullptr;

    double currentSampleRate = 44100.0;
    bool   wasPlaying = false;   // audio-thread only — detects the play→stop edge to silence voices

    // Hot-swap: parsed preset trees per lane + one full preset, committed at the pattern wrap.
    void applyTrackTree(int lane, const juce::ValueTree& tree);
    mu_hotswap::BarLineSwapper<juce::ValueTree, kNumChannels> hotSwap;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace mu_on
