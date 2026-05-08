#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Sequencer/SequencerEngine.h"
#include "Audio/VoiceEngine.h"
#include "Audio/MidiOutputEngine.h"
#include "FX/FXChain.h"
#include "Audio/MixerEngine.h"
#include "License/LicenseChecker.h"
#include "MidiPresetMap.h"

#include <memory>
#include <vector>
#include <unordered_map>

class PluginProcessor : public juce::AudioProcessor,
                        private juce::AudioProcessorValueTreeState::Listener,
                        private juce::AsyncUpdater
{
public:
    // Controls when a staged rhythm preset is committed to the live slot.
    enum class SwapMode { OnMasterLoop = 0, OnRhythmLoop = 1 };

    // apvts must be the first data member — initialized first, destroyed last.
    juce::AudioProcessorValueTreeState apvts;
    juce::StringArray                  loadedSamplePaths;  // [MaxRhythms]

    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    // True for both DAW and standalone:
    //   - Standalone: MIDI clock sync (#126).
    //   - DAW (VST3/CLAP): program-change → preset hot-swap (#127).
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override
    {
#if MUCLID_LITE_BUILD
        return true;
#else
        return false;
#endif
    }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    void   toggleInternalPlay()        { internalPlaying = !internalPlaying; if (!internalPlaying) internalBeatPos = 0.0; }
    bool   isInternalPlaying()   const { return internalPlaying; }
    void   setInternalBpm(double bpm)  { internalBpm = juce::jlimit(20.0, 300.0, bpm); }
    double getInternalBpm()      const { return internalBpm; }
    double getInternalBeatPos()  const
    {
        if (midiSyncEnabled.load(std::memory_order_relaxed) && midiClockIsPlaying.get())
            return midiClockBeatPosUI.load(std::memory_order_relaxed);
        return internalBeatPos;
    }

    // Multi-bus output (DAW only). Toggle is read at host scan-time; toggling at runtime
    // requires the host to rescan/reload the plugin to pick up the new bus configuration.
    void   setMultiBusEnabled(bool on);
    bool   getMultiBusEnabled() const { return multiBusEnabled.load(std::memory_order_relaxed); }

    static constexpr int kMasterBusIndex   = 0;
    static constexpr int kFirstDirectOutBus = 1;   // Out 1 = bus 1 ... Out 8 = bus 8
    static constexpr int kFXReturnsBusIndex = 9;
    static constexpr int kTotalBuses        = 10;

    // Rhythms 1..kAutomatedRhythms get full "Rhythm N" names in the APVTS parameter
    // layout so DAW automation lanes show them clearly. Remaining rhythms use short
    // "RN" names. Raise this constant (up to MaxRhythms) to expose more rhythms.
    static constexpr int kAutomatedRhythms  = 3;

    // MIDI clock sync (standalone only).
    void   setMidiSyncEnabled(bool on);
    bool   getMidiSyncEnabled()  const { return midiSyncEnabled.load(std::memory_order_relaxed); }
    void   setMidiSyncMessages(int mode);
    int    getMidiSyncMessages() const { return midiSyncMessages.load(std::memory_order_relaxed); }
    bool   isMidiClockPlaying()  const { return midiClockIsPlaying.get(); }
    double getMidiClockBpm()     const { return midiClockBpmEst.get(); }

    void    addRhythm    (const Rhythm& r);
    void    removeRhythm (int index);
    bool    swapRhythms  (int i, int j);
    Rhythm& getRhythm    (int index)       { return sequencer.getRhythm(index); }
    int     getNumRhythms() const          { return sequencer.getNumRhythms(); }
    void    updatePattern (int index)      { sequencer.updatePattern(index); }

    void loadSampleForRhythm(int rhythmIndex, const juce::File& file);

    // Sample preview — plays a file through the master output without assigning it.
    // Safe to call from the message thread at any time.
    void startSamplePreview(const juce::File& file);
    void stopSamplePreview();

    juce::String getSampleName(int rhythmIndex) const
    {
        if (rhythmIndex < 0 || rhythmIndex >= loadedSamplePaths.size()) return {};
        const auto& path = loadedSamplePaths[rhythmIndex];
        return path.isEmpty() ? juce::String() : juce::File(path).getFileName();
    }

    // Hot-swap staging: stages a rhythm preset for atomic commit at the next loop boundary.
    // If the sequencer is not playing, applies the preset immediately instead.
    void stageRhythmPreset (int rhythmIndex, const juce::File& presetFile);
    void cancelStagedSwap  (int rhythmIndex);
    bool hasPendingSwap    (int rhythmIndex) const;
    void setSwapMode(SwapMode m) { swapModeAtomic.store((int)m, std::memory_order_relaxed); }
    SwapMode getSwapMode() const { return static_cast<SwapMode>(swapModeAtomic.load(std::memory_order_relaxed)); }

    juce::File getContentDir() const;
    juce::File getPresetsDir() const;
    juce::File getRhythmsDir() const;
    juce::File getSamplesDir() const;
    void setContentDir(const juce::File& dir);
    void ensureContentFoldersExist();

    // License — checked once at startup; result is immutable thereafter.
    LicenseChecker::Info licenseInfo;
    bool isLicensed() const { return kBetaBuild || licenseInfo.status == LicenseStatus::Licensed; }

    void savePreset(const juce::String& name, const juce::String& description,
                    const juce::String& category, bool embedSamples = false);
    void loadPreset(const juce::File& file);
    void saveRhythmPreset(int rhythmIndex, const juce::String& name, const juce::String& category);
    void saveRhythmPresetToFile(int rhythmIndex, const juce::File& destFile, bool embedSample = false);
    bool applyRhythmPreset(const juce::File& file, int rhythmIndex);
    bool applyDefaultRhythm(int rhythmIndex);
    void loadDefaultPreset();

    SequencerEngine sequencer;
    // Fixed-size arrays so the audio thread never races with a vector reallocation
    // caused by addRhythm/removeRhythm on the message thread.  numActiveRhythms is
    // the authoritative count; processBlock reads it atomically once per block.
    std::array<std::unique_ptr<VoiceEngine>, SequencerEngine::MaxRhythms> voiceEngines;
    std::array<MidiOutputEngine,             SequencerEngine::MaxRhythms> midiEngines;
    std::atomic<int> numActiveRhythms { 0 };
    FXChain     fxChain;
    MixerEngine mixerEngine;

    // Play-state atomics: written by audio thread, read by UI at 30 Hz.
    struct RhythmPlayState
    {
        juce::Atomic<int>  currentStep   { 0 };
        juce::Atomic<int>  patternLength { 1 };
        juce::Atomic<int>  stepsA        { 1 }; // individual ring step counts for per-ring rotation
        juce::Atomic<int>  stepsB        { 1 };
        juce::Atomic<int>  stepsC        { 1 };
        juce::Atomic<bool> hitFired      { false }; // legacy: kept for backward compat; new code uses hitCount
        juce::Atomic<int>  hitCount      { 0 };     // monotonic counter — audio thread increments per hit, UI tracks lastSeen (Issue #43: avoids one-shot-flag race between RhythmCircle + SidebarItem)
    };
    std::array<RhythmPlayState, SequencerEngine::MaxRhythms> rhythmPlayState;
    juce::Atomic<float>  beatFraction     { 0.0f }; // fractional position within the current 1/16 step
    juce::Atomic<bool>   sequencerPlaying { false };
    juce::Atomic<double> lastBeatPos      { 0.0 };  // most recent beat position (for UI playhead)

    // Issue #133: per-destination modulated-value snapshot for the live-arc indicator.
    // Written by the audio thread after ModulationMatrix::process(); read by VoiceSection at 30 Hz.
    // Values are pre-normalized 0..1 to the knob's display range.
    enum ModSnapIdx : int {
        kSnapAmpAtk = 0, kSnapAmpDec, kSnapAmpSus, kSnapAmpRel,
        kSnapFilterCutoff, kSnapFilterRes,
        kSnapFenvAtk, kSnapFenvDec, kSnapFenvDepth,
        kSnapPitchSemi,
        kSnapPitchOct,   // #142: pitch octave live arc
        kSnapPitchFine,  // #142: pitch fine live arc
        kSnapInsDrive, kSnapInsOutput, kSnapInsBits, kSnapInsDither, kSnapInsLpf,
        kSnapCount
    };
    std::array<juce::Atomic<float>, kSnapCount> modSnapshot[SequencerEngine::MaxRhythms];

    // 128-entry MIDI program-change → .muRhyth preset path map. Public so the UI panel
    // can read/write directly. All mutation is message-thread-only; audio thread reads
    // only the channel mask atomic for gating.
    MidiPresetMap midiPresetMap;

private:
    // Hot-swap state: message thread writes pendingRhythm/pendingVoice, then sets isReady.
    // Audio thread detects a loop boundary, sets boundaryReached, triggers handleAsyncUpdate.
    // handleAsyncUpdate runs on message thread and performs the actual swap.
    struct PendingRhythmSwap
    {
        Rhythm                       pendingRhythm;
        juce::String                 pendingSamplePath;
        std::unique_ptr<VoiceEngine> pendingVoice;
        std::atomic<bool> isReady         { false }; // set by message thread after staging
        std::atomic<bool> boundaryReached { false }; // set by audio thread at loop boundary

        PendingRhythmSwap() = default;
        PendingRhythmSwap(const PendingRhythmSwap&) = delete;
        PendingRhythmSwap& operator=(const PendingRhythmSwap&) = delete;
    };

    std::array<PendingRhythmSwap, SequencerEngine::MaxRhythms> pendingSwaps;
    std::atomic<int> swapModeAtomic { 0 }; // 0 = OnMasterLoop, 1 = OnRhythmLoop

    // MIDI program-change queue: audio thread enqueues on incoming program-change,
    // handleAsyncUpdate (message thread) drains and calls stageRhythmPreset.
    struct ProgramChangeEvent { int slot; int presetIndex; };
    static constexpr int kPCFifoSize = 32;
    juce::AbstractFifo                              pcFifo { kPCFifoSize };
    std::array<ProgramChangeEvent, kPCFifoSize>     pcQueue {};

    void handleAsyncUpdate() override;

    std::unique_ptr<juce::PropertiesFile> appSettings;

    bool apvtsLoading = false;

    // suspendProcessing() sets a flag but does NOT block until the current
    // processBlock callback finishes. rhythmsLock provides the missing barrier.
    juce::CriticalSection rhythmsLock;

    // Multi-bus output (DAW). Read by isBusesLayoutSupported at host scan-time;
    // persisted to appSettings so it survives across plugin instances.
    std::atomic<bool> multiBusEnabled { true };

    // MIDI clock sync state (standalone only).
    // Atomics are written by either thread; plain members are audio-thread only.
    std::atomic<bool> midiSyncEnabled  { false };
    std::atomic<int>  midiSyncMessages { 2 };         // 0=clock, 1=transport, 2=both
    juce::Atomic<bool>   midiClockIsPlaying  { false };
    juce::Atomic<double> midiClockBpmEst     { 120.0 };
    std::atomic<double>  midiClockBeatPosUI  { 0.0 };  // written by audio thread, read by UI
    // Audio-thread-only:
    double midiClockBeatPos              = 0.0;
    int    midiClockSamplesSinceLastTick = 0;
    std::array<int, 24> midiClockTickIntervals {};
    int    midiClockRingHead             = 0;
    int    midiClockRingCount            = 0;

    // Pre-allocated modulation parameter map — reused every block to avoid audio-thread allocation.
    // Keys match ModDest::ids.  Values are initialised in constructor and updated each block.
    std::unordered_map<std::string, float> modParamValues;

    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void syncRhythmParam(int ri, const juce::String& suffix, float v);
    void syncFXParam(const juce::String& id, float v);
    void syncMixerParam(const juce::String& id, float v);
    void pushRhythmToAPVTS(int ri);
    void pushMixerChannelToAPVTS(int idx);
    void swapAPVTSForRhythms(int i, int j);
    void resetPlayState(int idx);
    void restoreStateFromTree(const juce::ValueTree& state);

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Sample preview (for file browser audition — routes through master output).
    juce::AudioFormatManager previewFormatManager;
    juce::AudioTransportSource previewTransport;
    std::unique_ptr<juce::AudioFormatReaderSource> previewSource;
    juce::AudioBuffer<float> previewScratchBuffer;

    bool   internalPlaying   = false;
    double internalBeatPos   = 0.0;
    double internalBpm       = 120.0;
    double currentSampleRate = 44100.0;
    int    currentBlockSize  = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
