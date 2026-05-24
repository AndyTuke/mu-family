#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "ProcessorBase.h"
#include "Sequencer/SequencerEngine.h"
#include "Audio/VoiceEngine.h"
#include "Audio/MidiOutputEngine.h"
#include "Audio/FX/Slots/FXChain.h"
#include "Audio/MixerEngine.h"
#include "License/LicenseChecker.h"
#include "Persistence/MidiPresetMap.h"
#include "MuLimits.h"
#include "Modulation/ModulationSnapshot.h"
#include "SamplePreview.h"
#include "MidiClockSync.h"
#include "PresetIO.h"
#include "HotSwapStager.h"

#include <memory>
#include <vector>
#include <unordered_map>

class PluginProcessor : public ProcessorBase,
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

    void getStateInformation(juce::MemoryBlock& d) override { presetIO.getStateInformation(d); }
    void setStateInformation(const void* d, int s) override { presetIO.setStateInformation(d, s); }

    // internalPlaying / internalBeatPos are touched by both audio (write) and
    // message thread (read + clear). std::atomic<> with relaxed ordering — no other
    // memory is published through them, so relaxed is sufficient.
    void   toggleInternalPlay()
    {
        const bool nowPlaying = !internalPlaying.load(std::memory_order_relaxed);
        internalPlaying.store(nowPlaying, std::memory_order_relaxed);
        if (!nowPlaying) internalBeatPos.store(0.0, std::memory_order_relaxed);
    }
    bool   isInternalPlaying()   const { return internalPlaying.load(std::memory_order_relaxed); }
    void   setInternalBpm(double bpm)  { internalBpm = juce::jlimit(20.0, 300.0, bpm); }
    double getInternalBpm()      const { return internalBpm; }
    double getInternalBeatPos()  const
    {
        if (midiClockSync.isEnabled() && midiClockSync.isPlaying())
            return midiClockSync.getBeatPosUI();
        return internalBeatPos.load(std::memory_order_relaxed);
    }

    // Multi-bus output (DAW only). Toggle is read at host scan-time; toggling at runtime
    // requires the host to rescan/reload the plugin to pick up the new bus configuration.
    void   setMultiBusEnabled(bool on);
    bool   getMultiBusEnabled() const { return multiBusEnabled.load(std::memory_order_relaxed); }

    // UI scale (Medium=1.0, Large=1.25). Persisted via appSettings. Editor reads
    // at ctor time and applies to mu_ui::scale BEFORE constructing children so
    // ctor-time fonts (#574) pick up the right size on a fresh open. Runtime
    // changes go via setUiScale → onUiScaleChanged callback so the editor can
    // resize + relayout live (with a "reopen for fonts" hint near the picker).
    static constexpr float kUiScaleMedium = 1.0f;
    static constexpr float kUiScaleLarge  = 1.25f;
    void  setUiScale(float scale);
    float getUiScale() const noexcept { return uiScale; }
    // Editor registers here to react to a settings-overlay-driven scale change.
    std::function<void(float)> onUiScaleChanged;

    static constexpr int kMasterBusIndex   = 0;
    static constexpr int kFirstDirectOutBus = 1;   // Out 1 = bus 1 ... Out 8 = bus 8
    static constexpr int kFXReturnsBusIndex = 9;
    static constexpr int kTotalBuses        = 10;

    static constexpr int kAutomatedRhythms  = mu_limits::kMaxAutomatedRhythms;

    // MIDI clock sync (standalone only).
    void   setMidiSyncEnabled(bool on);
    bool   getMidiSyncEnabled()  const { return midiClockSync.isEnabled(); }
    void   setMidiSyncMessages(int mode);
    int    getMidiSyncMessages() const { return midiClockSync.getMessages(); }
    bool   isMidiClockPlaying()  const { return midiClockSync.isPlaying(); }
    double getMidiClockBpm()     const { return midiClockSync.getBpm(); }

    void    addRhythm    (const Rhythm& r);
    void    removeRhythm (int index);
    bool    swapRhythms  (int i, int j);
    // reset rhythm to defaults preserving name + colour. Uses suspendProcessing
    // + rhythmsLock so the message thread doesn't spin on modLock while the audio
    // thread holds it. Same concurrency pattern as removeRhythm / swapRhythmSlots.
    void    resetRhythm  (int index);
    // rename rhythm under rhythmsLock — no audio-thread reads of name today,
    // but the lock is the project's canonical "message thread mutates Rhythm" pattern
    // and avoids the UI-thread modLock spin if a future MIDI/PC matcher reads name.
    void    renameRhythm (int index, const juce::String& newName);
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

    // True when the rhythm has a sample path recorded but the voice engine couldn't
    // load it (e.g. preset was saved with a linked sample whose file was later moved
    // or deleted). The RhythmPanel sample bar uses this to show a "missing — click
    // to find" affordance instead of silently leaving the slot empty.
    bool isSampleMissing(int rhythmIndex) const
    {
        if (rhythmIndex < 0 || rhythmIndex >= loadedSamplePaths.size()) return false;
        if (loadedSamplePaths[rhythmIndex].isEmpty()) return false;
        if (rhythmIndex >= (int)voiceEngines.size() || !voiceEngines[rhythmIndex]) return true;
        return !voiceEngines[rhythmIndex]->hasSample();
    }

    // Hot-swap staging: stages a rhythm preset for atomic commit at the next loop boundary.
    // If the sequencer is not playing, applies the preset immediately instead.
    void stageRhythmPreset(int ri, const juce::File& f)  { presetIO.stageRhythmPreset(ri, f); }
    void cancelStagedSwap (int ri)                        { hotSwapStager.cancelStagedSwap(ri); }
    bool hasPendingSwap   (int ri) const                  { return hotSwapStager.hasPendingSwap(ri); }

    // fired (on the message thread, from handleAsyncUpdate) after a hot-swap
    // commit finishes. The editor uses this to refresh non-APVTS UI state — name
    // label, sample bar, colour-tinted bits — that pushRhythmToAPVTS doesn't cover
    // because those fields aren't APVTS parameters. Editor MUST clear this in its
    // destructor: the processor can outlive the editor (DAW close-window-keep-
    // plugin), and a swap commit firing into a destroyed editor is a UAF.
    std::function<void(int rhythmIndex)> onRhythmHotSwapCommitted;

    // fired when a preset / state-restore parse fails (parseXML returns null
    // or the resulting tree is invalid). Editor surfaces via the status bar so the
    // user sees feedback instead of a silent click. Same dtor-cleanup requirement
    // as onRhythmHotSwapCommitted — clear in editor dtor to avoid UAF when the
    // processor outlives the editor.
    std::function<void(const juce::String& message)> onLoadError;
    void setSwapMode(SwapMode m) { swapModeAtomic.store((int)m, std::memory_order_relaxed); }
    SwapMode getSwapMode() const { return static_cast<SwapMode>(swapModeAtomic.load(std::memory_order_relaxed)); }

    juce::File getContentDir() const;
    juce::File getPresetsDir() const;
    juce::File getRhythmsDir() const;
    juce::File getSamplesDir() const;
    void setContentDir(const juce::File& dir);
    void ensureContentFoldersExist();

    // user-configurable personal sample library. Distinct from the
    // content / My Documents folder (which hosts factory + preset-linked
    // material). Default when unset = OS user Music dir. setPrimarySampleDir(
    // juce::File{}) clears the override and reverts to that default.
    juce::File getPrimarySampleDir() const;
    void       setPrimarySampleDir(const juce::File& dir);

    // License — checked once at startup; result is immutable thereafter.
    LicenseChecker::Info licenseInfo;
    bool isLicensed() const { return kBetaBuild || licenseInfo.status == LicenseStatus::Licensed; }

    // Set by PluginEditor (standalone only): shows the SaveDialog then calls the quit callback on
    // completion, or drops it if the user cancels. Allows the close-confirmation dialog to trigger
    // a proper "Save As" flow before quitting rather than a raw state dump.
    std::function<void(std::function<void()>)> onSaveAndQuit;

    void savePreset(const juce::String& n, const juce::String& d,
                    const juce::String& c, bool e = false) { presetIO.savePreset(n, d, c, e); }
    void loadPreset(const juce::File& f)  { presetIO.loadPreset(f); }
    void saveRhythmPresetToFile(int ri, const juce::File& dest,
                                bool emb = false, const juce::String& cat = {},
                                const juce::String& desc = {})
                                { presetIO.saveRhythmPresetToFile(ri, dest, emb, cat, desc); }

    // Shared preset category list.
    juce::StringArray loadCategoryList() const   { return presetIO.loadCategoryList(); }
    void ensureCategoryInList(const juce::String& c) { presetIO.ensureCategoryInList(c); }
    bool applyRhythmPreset(const juce::File& f, int ri) { return presetIO.applyRhythmPreset(f, ri); }
    bool applyDefaultRhythm(int ri)              { return presetIO.applyDefaultRhythm(ri); }
    void loadDefaultPreset()                     { presetIO.loadDefaultPreset(); }

    SequencerEngine sequencer;
    // Fixed-size arrays so the audio thread never races with a vector reallocation
    // caused by addRhythm/removeRhythm on the message thread.  numActiveRhythms is
    // the authoritative count; processBlock reads it atomically once per block.
    std::array<std::unique_ptr<VoiceEngine>, SequencerEngine::MaxRhythms> voiceEngines;
    std::array<MidiOutputEngine,             SequencerEngine::MaxRhythms> midiEngines;
    std::atomic<int> numActiveRhythms { 0 };
    // fxChain and mixerEngine are inherited from ProcessorBase.

    // Stage 34: per-rhythm retired voice engines that continue rendering their
    // in-flight sample / envelope tail after a hot-swap. Step 2 (this commit)
    // wires the storage + render loop + cleanup-flag drain with nothing
    // populating the slots, so behaviour is unchanged. Step 3 wires retire-on-
    // swap, which is what actually populates these. When an engine reports
    // isFullyDrained(), the audio thread store-releases retiredReadyForCleanup;
    // the message thread (handleAsyncUpdate) polls + clears the flag under
    // suspendProcessing and destroys the engine off the RT thread.
    static constexpr int kMaxRetiredEngines = mu_limits::kMaxRetiredVoiceEngines;
    std::array<std::array<std::unique_ptr<VoiceEngine>, kMaxRetiredEngines>,
               SequencerEngine::MaxRhythms> retiredVoiceEngines;
    std::array<std::array<std::atomic<bool>, kMaxRetiredEngines>,
               SequencerEngine::MaxRhythms> retiredReadyForCleanup;

    // Play-state atomics: written by audio thread, read by UI at 30 Hz.
    struct RhythmPlayState
    {
        std::atomic<int>  currentStep   { 0 };
        std::atomic<int>  currentStepC  { 0 }; // effectiveStep % stepsC — independent of combined-pattern length
        std::atomic<int>  patternLength { 1 };
        std::atomic<int>  stepsA        { 1 }; // individual ring step counts for per-ring rotation
        std::atomic<int>  stepsB        { 1 };
        std::atomic<int>  stepsC        { 1 };
        std::atomic<int>  hitCount      { 0 };     // monotonic counter — audio thread increments per hit, UI tracks lastSeen (Issue #43: avoids one-shot-flag race between RhythmCircle + SidebarItem)
    };
    std::array<RhythmPlayState, SequencerEngine::MaxRhythms> rhythmPlayState;
    std::atomic<float>  beatFraction     { 0.0f }; // fractional position within the current 1/16 step
    std::atomic<bool>   sequencerPlaying { false };
    std::atomic<double> lastBeatPos      { 0.0 };  // most recent beat position (for UI playhead)

    // Snapshot accessor for UI panels (see Modulation/ModulationSnapshot.h for index enum).
    float getModSnapshot(int rhythmIdx, int snapIdx) const noexcept
    {
        return modSnapshot[rhythmIdx][snapIdx].load();
    }

    // GR-meter pointer for the per-voice compressor/limiter insert UI knob.
    // Returns nullptr when the rhythm index is out of range or the engine is null.
    std::atomic<float>* getInsertGRReductionPtr(int ri)
    {
        if (ri < 0 || ri >= getNumRhythms() || !voiceEngines[(size_t)ri]) return nullptr;
        return &voiceEngines[(size_t)ri]->insertProc.grReduction;
    }

    // 128-entry MIDI program-change → .muRhyth preset path map. Public so the UI panel
    // can read/write directly. All mutation is message-thread-only; audio thread reads
    // only the channel mask atomic for gating.
    MidiPresetMap midiPresetMap;

private:
    std::array<std::atomic<float>, kSnapCount> modSnapshot[SequencerEngine::MaxRhythms];

    std::atomic<int> swapModeAtomic { 0 }; // 0 = OnMasterLoop, 1 = OnRhythmLoop; read by HotSwapStager

    // MIDI program-change queue: audio thread enqueues on incoming program-change,
    // handleAsyncUpdate (message thread) drains and calls stageRhythmPreset.
    struct ProgramChangeEvent { int slot; int presetIndex; };
    static constexpr int kPCFifoSize = mu_limits::kProgramChangeFifoSize;
    juce::AbstractFifo                              pcFifo { kPCFifoSize };
    std::array<ProgramChangeEvent, kPCFifoSize>     pcQueue {};

    void handleAsyncUpdate() override;

    std::unique_ptr<juce::PropertiesFile> appSettings;

    // now atomic — listeners can fire on the audio thread when a DAW runs
    // host automation, so the cross-thread read in syncRhythmParam needs proper
    // ordering. All set/clear pairs go through `mu_core::ScopedApvtsLoading` so
    // an exception inside the bulk push can't latch the flag at true.
    std::atomic<bool> apvtsLoading { false };

public:
    // exposed so UI listeners can skip their per-param refresh during a
    // bulk load (state restore, swap commit, swap-rhythms reorder). The
    // bulk-load orchestrator handles the full UI refresh afterwards, so the
    // per-param refresh during the bulk push is pure waste.
    bool isApvtsLoading() const noexcept
    {
        return apvtsLoading.load(std::memory_order_acquire);
    }
private:

    // suspendProcessing() sets a flag but does NOT block until the current
    // processBlock callback finishes. rhythmsLock provides the missing barrier.
    juce::CriticalSection rhythmsLock;

    // Multi-bus output (DAW). Read by isBusesLayoutSupported at host scan-time;
    // persisted to appSettings so it survives across plugin instances.
    std::atomic<bool> multiBusEnabled { true };

    // UI scale (Medium=1.0 baseline, Large=1.25). Persisted via appSettings;
    // editor consults at ctor + reacts to runtime changes via onUiScaleChanged.
    float uiScale { kUiScaleMedium };

    MidiClockSync midiClockSync;

    // Pre-allocated modulation parameter map — reused every block to avoid audio-thread allocation.
    // Keys match ModDest::ids. Values are initialised in constructor and updated each block.
    // keyed by `std::string_view` rather than `std::string`. All write/read sites use
    // `const char*` string literals (`modParamValues["amp.attack"] = …`), which previously
    // constructed a temp std::string per access (~30 × N rhythms × 690 blocks/sec on the
    // audio thread). string_view of a literal is alloc-free; the literal's static storage
    // guarantees the view stays valid for the lifetime of the map entry. ModulationMatrix's
    // `find(a.destinationId)` still works because std::string converts implicitly.
    std::unordered_map<std::string_view, float> modParamValues;

    // per-rhythm modulated euclid pattern overrides — written by the audio thread
    // after ModulationMatrix::process(). Used by the audio-thread pattern recompute path
    // (Stage B) and the UI live-arc indicator (Stage C). Values are integer-rounded so
    // change-detection skips no-op recomputes. Struct definitions live in Rhythm.h /
    // HitGenerator.h so Sequencer-layer code can reuse them.
    std::array<EuclidOverrides, SequencerEngine::MaxRhythms> lastEuclidOverrides;
    // Previous block's overrides per rhythm — Stage B compares against this to skip
    // pattern recomputes when nothing crossed an integer boundary.
    std::array<EuclidOverrides, SequencerEngine::MaxRhythms> prevEuclidOverrides;
    // tracks whether the rhythm's modulation matrix had any assignments LAST block.
    // The modulation pass is gated on (matrix non-empty || transition-to-empty), so we
    // skip the seed + matrix.process + write-back work for never-modulated rhythms but
    // still run one final reset pass after assignment removal to flush lastEuclidOverrides
    // back to base values, which Stage B then picks up via change-detection.
    std::array<bool, SequencerEngine::MaxRhythms> prevMatrixHadAssignments {};

    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void syncRhythmParam(int ri, const juce::String& suffix, float v);
    void syncFXParam(const juce::String& id, float v);

    // Cached APVTS atomic pointers for the per-sync-param family reads in
    // syncFXParam. parameterChanged for dly_syncDenom/Dot/Trip and
    // echo_syncDenom/Dot/Trip needs the sibling values to call
    // DelaySlot::setTimeDivision(denoms[idx], dot, tri); without caching, that
    // path does three string-keyed hash lookups per host-automation tick on
    // potentially the audio thread. Pointers are populated once in the ctor
    // (after createParameterLayout() runs) and stay valid for the processor's
    // lifetime — APVTS owns the underlying juce::AudioParameter objects.
    std::atomic<float>* dlySyncDenomPtr = nullptr;
    std::atomic<float>* dlySyncDotPtr   = nullptr;
    std::atomic<float>* dlySyncTripPtr  = nullptr;
    std::atomic<float>* echoSyncDenomPtr = nullptr;
    std::atomic<float>* echoSyncDotPtr   = nullptr;
    std::atomic<float>* echoSyncTripPtr  = nullptr;
    void syncMixerParam(const juce::String& id, float v);
    void pushRhythmToAPVTS(int ri);
    // Force-applies all APVTS r{i}_ params back into Rhythm fields, bypassing
    // JUCE's unchanged-value parameterChanged-skip. Required after sequencer
    // shrink/grow cycles (preset A → B → A) where Rhythm objects are destroyed
    // and recreated with default fields but APVTS values stay the same — no
    // listener fires, so r.voiceParams / r.genA.hits never repopulate.
    void forceSyncRhythmFromAPVTS(int ri);
    void pushMixerChannelToAPVTS(int idx);
    void swapAPVTSForRhythms(int i, int j);
    void resetPlayState(int idx);
    void restoreStateFromTree(const juce::ValueTree& s) { presetIO.restoreStateFromTree(s); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    SamplePreview  samplePreview;
    PresetIO       presetIO      { *this };
    HotSwapStager  hotSwapStager { *this };

    friend class PresetIO;
    friend class HotSwapStager;

    // atomic for safe cross-thread access (audio writes, UI reads + clears).
    std::atomic<bool>   internalPlaying   { false };
    std::atomic<double> internalBeatPos   { 0.0 };
    double              internalBpm       = 120.0;   // message-thread only
    double currentSampleRate = 44100.0;
    int    currentBlockSize  = 512;

#if MUCLID_LITE_BUILD
    // Cached APVTS atomic pointers for the LITE build's per-block reads.
    // Resolved once at construction (after APVTS layout is registered) so
    // processBlock doesn't pay a hash lookup + literal-string materialise per call.
    std::atomic<float>* liteMidiNotePtr  = nullptr;
    std::atomic<float>* liteAccentAmtPtr = nullptr;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
