#pragma once

#include "Plugin/ProcessorBase.h"        // mu-core base
#include "Plugin/MixerFxParams.h"         // mu-core: shared global-FX / mixer APVTS layout
#include "Plugin/MidiClockSync.h"         // mu-core: shared MIDI-clock slave
#include "Plugin/MuOnChannels.h"
#include "Sequencer/StepPattern.h"
#include "Sequencer/GrooveSequencer.h"
#include "Audio/GrooveVoices.h"
#include "Sequencer/VoiceSlot.h"   // mu-core: per-lane modulation slot

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

    // Persist UI scale through appSettings (mirrors mu-tant) so a fresh open keeps size.
    void setUiScale(float scale) override;

    // ── MIDI clock sync (standalone) ──────────────────────────────────────────
    // Slaves the sequencer beat/tempo to external MIDI clock via the shared mu-core
    // MidiClockSync (standard synth feature). Setters persist to appSettings + drive
    // the engine.
    bool   getMidiSyncEnabled()  const override { return midiClockSync.isEnabled(); }
    int    getMidiSyncMessages() const override { return midiClockSync.getMessages(); }
    bool   isMidiClockPlaying()  const override { return midiClockSync.isPlaying(); }
    double getMidiClockBpm()     const override { return midiClockSync.getBpm(); }
    void   setMidiSyncEnabled(bool on);
    void   setMidiSyncMessages(int mode);

    // ── ProcessorBase channel metadata (drives sidebar + mixer) ───────────────
    int          getNumChannels()              const override { return kNumChannels; }
    juce::String getChannelName(int idx)       const override
    {
        switch (idx) { case Kick: return "Kick"; case Bass: return "Bass";
                       case Hat: return "Hat";   case Snare: return "Snare";
                       case Rumble: return "Rumble"; default: return {}; }
    }
    // Distinct palette entries per instrument lane.
    int          getChannelColourIndex(int idx) const override
    {
        static constexpr int kColours[kNumChannels] = { 0, 2, 5, 7, 4 };  // Kick/Bass/Hat/Snare/Rumble
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
    VoiceSlot& voiceSlot(int lane) noexcept { return voiceSlots[(size_t) juce::jlimit(0, kNumChannels - 1, lane)]; }
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

    // Per-lane modulator (de)serialise into a <VoiceData> child of the state tree
    // (mirrors mu-tant; rides the APVTS state alongside the <Pattern> grid).
    void writeVoiceDataToState(juce::ValueTree& state);
    void readVoiceDataFromState(const juce::ValueTree& state);

    // Per-channel render hook handed to the shared MixerEngine — fills each lane's buffer
    // from its engine (Kick/Bass/Hat/Snare) via GrooveVoices.
    MixerEngine::RenderChannelFn renderChannelCb;

    // 909 sequencer — owns the grid pattern; clocked off the internal transport each block.
    StepPattern     stepPattern;
    GrooveSequencer sequencer { stepPattern };
    GrooveVoices    grooveVoices;                              // the four instrument engines
    std::array<VoiceSlot, kNumChannels> voiceSlots;            // per-lane modulation (ControlSequences + matrix)
    ControlSequence  rumbleEnv;                                // Rumble lane drawable bar-volume envelope
    CopyableSpinLock rumbleEnvLock;                            // guards rumbleEnv (msg edit ↔ audio eval)
    std::array<std::atomic<int>, kNumChannels> triggers { };   // per-lane trigger counter (UI pulse)

    // Cached APVTS pointers for the product sequencer params (read each block).
    std::atomic<float>* seqSwingParam  = nullptr;
    std::atomic<float>* seqAccentParam = nullptr;

    std::atomic<bool>   playing { false };
    std::atomic<double> internalBeatPos { 0.0 };
    std::atomic<double> internalBpm { 120.0 };
    double currentSampleRate = 44100.0;
    bool   wasPlaying = false;   // audio-thread only — detects the play→stop edge to silence voices

    // Shared MIDI-clock slave (standalone). process() scans the MIDI buffer each block;
    // when enabled + playing, processBlock slaves the sequencer beat/tempo to it.
    MidiClockSync midiClockSync;

    // Persistent app settings (UI scale + MIDI-clock prefs) — mirrors mu-tant.
    std::unique_ptr<juce::PropertiesFile> appSettings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace mu_on
