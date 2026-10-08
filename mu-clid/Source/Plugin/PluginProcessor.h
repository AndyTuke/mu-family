#pragma once

#include "Modulation/ModulationDestinations.h"   // ModDest::kTableSize (modSlot)
#include <array>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Plugin/ProcessorBase.h"
#include "Sequencer/SequencerEngine.h"
#include "Audio/VoiceEngine.h"
#include "Audio/MidiOutputEngine.h"
#include "Audio/FX/Slots/FXChain.h"
#include "Audio/MixerEngine.h"
#include "License/LicenseKey.h"       // product: mu-Clid id + filename + public key
#include "MuLimits.h"
#include "Modulation/ModulationSnapshot.h"
#include "SampleLibrary.h"
#include "RhythmManager.h"
#include "Plugin/MidiClockSync.h"   // shared mu-core MIDI-clock slave
#include "PresetIO.h"
#include "HotSwapStager.h"

#include <memory>
#include <vector>
#include <unordered_map>

class PluginProcessor : public ProcessorBase,
                        private juce::AudioProcessorValueTreeState::Listener
{
public:
    // Controls when a staged rhythm preset is committed to the live slot.
    enum class SwapMode { OnMasterLoop = 0, OnRhythmLoop = 1 };

    // `apvts` lives on ProcessorBase (mu-core) — every mu-family plugin shares
    // one. Layout is supplied via createParameterLayout() when the base ctor
    // runs. PluginProcessor's own members below may reference apvts; they
    // initialize after the base, so the reference is safe.

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
    //   - Standalone: MIDI clock sync.
    //   - DAW (VST3/CLAP): program-change → preset hot-swap.
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

    // Internal transport comes from ProcessorBase; the UI beat follows the MIDI clock when synced.
    double getInternalBeatPos()  const override
    {
        if (midiClockSync.isEnabled() && midiClockSync.isPlaying())
            return midiClockSync.getBeatPosUI();
        return internalBeatPos.load(std::memory_order_relaxed);
    }

    // Multi-bus output (DAW only). Toggle is read at host scan-time; toggling at runtime
    // requires the host to rescan/reload the plugin to pick up the new bus configuration.
    void   setMultiBusEnabled(bool on);
    bool   getMultiBusEnabled() const { return multiBusEnabled.load(std::memory_order_relaxed); }

    static constexpr int kMasterBusIndex   = 0;
    static constexpr int kFirstDirectOutBus = 1;   // Out 1 = bus 1 ... Out 8 = bus 8
    static constexpr int kFXReturnsBusIndex = 9;
    static constexpr int kTotalBuses        = 10;

    static constexpr int kAutomatedRhythms  = mu_limits::kMaxAutomatedChannels;

    // MIDI Note mode (plugin only). 0=Free (host transport drives play), 1=Note (Note On/Off drives play).
    void setMidiNoteMode(int mode);
    int  getMidiNoteMode() const { return midiNoteMode.load(std::memory_order_relaxed); }

    // Rhythm-slot add / remove / swap / reset / rename.
    RhythmManager rhythms { *this };

    Rhythm& getRhythm    (int index)       { return sequencer.getRhythm(index); }
    const Rhythm& getRhythm(int index) const { return sequencer.getRhythm(index); }
    int     getNumRhythms() const          { return sequencer.getNumRhythms(); }

    // ProcessorBase channel-metadata interface — mu-clid maps "channel" → "rhythm".
    int          getNumChannels()             const override { return getNumRhythms(); }
    juce::String getChannelName(int idx)      const override
    {
        return (idx >= 0 && idx < getNumRhythms()) ? juce::String(getRhythm(idx).name) : juce::String();
    }
    int          getChannelColourIndex(int idx) const override
    {
        return (idx >= 0 && idx < getNumRhythms()) ? getRhythm(idx).colourIndex : 0;
    }
    void    updatePattern (int index)      { sequencer.updatePattern(index); }

    // Per-rhythm sample paths, load / missing queries, preview player and primary sample folder.
    SampleLibrary samples { *this };

    // Hot-swap staging: stages a rhythm preset for atomic commit at the next loop boundary.
    // If the sequencer is not playing, applies the preset immediately instead.
    void stageRhythmPreset(int ri, const juce::File& f, bool keepIdentity = false)  { presetIO.stageRhythmPreset(ri, f, keepIdentity); }
    void cancelStagedSwap (int ri)                        { hotSwapStager.cancelStagedSwap(ri); }
    bool hasPendingSwap   (int ri) const override         { return hotSwapStager.hasPendingSwap(ri); }
    bool hasPendingFullPreset() const override            { return hotSwapStager.hasPendingFullPreset(); }
    int  getMasterLoopSteps() const override              { return sequencer.getMasterLoopSteps(); }
    int  getMasterLoopCurrentStep() const override        { return sequencer.getMasterLoopCurrentStep(); }

    // A rhythm hot-swap commit fires ProcessorBase::onSlotPresetCommitted: the editor refreshes
    // the non-APVTS UI state (name label, sample bar, colour-tinted bits) pushRhythmToAPVTS can't.

    // ProcessorBase slot-preset API: a rhythm preset into rhythm `ri`.
    void saveSlotPreset(int ri, const juce::String& name) override;
    void loadSlotPreset(int ri, const juce::File& f) override { stageRhythmPreset(ri, f); }
    void resetSlot(int ri) override                           { rhythms.reset(ri); }
    void setSwapMode(SwapMode m) { swapModeAtomic.store((int)m, std::memory_order_relaxed); }
    SwapMode getSwapMode() const { return static_cast<SwapMode>(swapModeAtomic.load(std::memory_order_relaxed)); }

    juce::File getRhythmsDir() const;
    juce::File getSamplesDir() const;
    void setContentDir(const juce::File& dir);
    void ensureContentFoldersExist();

    // Licensing is shared (ProcessorBase::initLicensing). Lite is always licensed — no
    // activation, no demo caps.
   #if MUCLID_LITE_BUILD
    bool isLicensed() const override { return true; }
   #endif
    // Activation lifts the 16-step demo cap straight away.
    void onActivated() override { sequencer.setStepCap(HitGenerator::kMaxSteps); }
    // Demo limits the unlicensed editor to a single rhythm of at most 16 steps.
    int demoMaxChannels() const override { return 1; }
    int demoMaxSteps()    const override { return 16; }

    void savePreset(const juce::String& n, const juce::String& d,
                    const juce::String& c, bool e = false) override { presetIO.savePreset(n, d, c, e); }
    void loadPreset(const juce::File& f) override { presetIO.loadPreset(f); publishPresetName(f.getFileNameWithoutExtension()); }
    void saveRhythmPresetToFile(int ri, const juce::File& dest,
                                bool emb = false, const juce::String& cat = {},
                                const juce::String& desc = {})
                                { presetIO.saveRhythmPresetToFile(ri, dest, emb, cat, desc); }

    // Shared preset category list.
    juce::StringArray loadCategoryList() const override  { return presetIO.loadCategoryList(); }
    void ensureCategoryInList(const juce::String& c) override { presetIO.ensureCategoryInList(c); }
    bool applyRhythmPreset(const juce::File& f, int ri) { return presetIO.applyRhythmPreset(f, ri); }
    bool applyDefaultRhythm(int ri)              { return presetIO.applyDefaultRhythm(ri); }
    // _default.muClid, falling back to a single-rhythm _default.muRhythm (PresetIO).
    void loadDefaultPreset() override            { presetIO.loadDefaultPreset(); }


    SequencerEngine sequencer;
    // Fixed-size arrays so the audio thread never races with a vector reallocation
    // caused by RhythmManager add / remove on the message thread.  numActiveRhythms is
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
        std::atomic<int>  hitCount      { 0 };     // monotonic counter — audio thread increments per hit, UI tracks lastSeen (avoids one-shot-flag race between RhythmCircle + SidebarItem)
    };
    std::array<RhythmPlayState, SequencerEngine::MaxRhythms> rhythmPlayState;
    std::atomic<float>  beatFraction     { 0.0f }; // fractional position within the current 1/16 step
    std::atomic<bool>   sequencerPlaying { false };
    std::atomic<double> lastBeatPos      { 0.0 };  // most recent beat position (for UI playhead)

    // Snapshot accessor for UI panels (see Modulation/ModulationSnapshot.h for index enum).
    float getModSnapshot(int rhythmIndex, int snapIndex) const noexcept
    {
        return modSnapshot[rhythmIndex][snapIndex].load();
    }

    // UI accessor for the per-rhythm modulated euclid overrides — read by RhythmPanel +
    // SidebarItem on a timer so the visual circles reflect modulation of hits/rotate/etc.
    //. Returns a struct copy; fields are integer-rounded by the audio thread so
    // torn reads are benign for visual display. Pre-fix the audio thread only
    // wrote `lastEuclidOverrides[r]` when the modulation pass actually ran, leaving the
    // overrides at C++-default zeros for any rhythm with no assignments — so the default
    // rhythm at startup drew no hits. Fall back to the rhythm's base gen values when no
    // modulation pass has touched the entry so `HitGenerator::getStepTypes(ov)` yields
    // the rhythm's actual pattern for unmodulated cases.
    EuclidOverrides getModulatedEuclidOverrides(int rhythmIndex) const noexcept
    {
        if (rhythmIndex < 0 || rhythmIndex >= (int) lastEuclidOverrides.size()
                          || rhythmIndex >= sequencer.getNumRhythms())
            return {};
        const Rhythm& r = sequencer.getRhythm(rhythmIndex);
        if (r.modulationMatrix.getAssignments().empty())
        {
            EuclidOverrides ov;
            auto fill = [](EuclidGenOverrides& g, const HitGenerator& src) {
                g.hits         = src.hits;
                g.rotate       = src.rotate;
                g.prePad       = src.prePad;
                g.postPad      = src.postPad;
                g.insertStart  = src.insertStart;
                g.insertLength = src.insertLength;
            };
            fill(ov.a, r.genA);
            fill(ov.b, r.genB);
            fill(ov.c, r.genC);
            return ov;
        }
        return lastEuclidOverrides[(size_t) rhythmIndex];
    }

    // GR-meter pointer for the per-voice compressor/limiter insert UI knob.
    // Returns nullptr when the rhythm index is out of range or the engine is null.
    std::atomic<float>* getInsertGRReductionPtr(int ri)
    {
        if (ri < 0 || ri >= getNumRhythms() || !voiceEngines[(size_t)ri]) return nullptr;
        return &voiceEngines[(size_t)ri]->insertProc.grReduction;
    }

private:
    std::array<std::atomic<float>, kSnapCount> modSnapshot[SequencerEngine::MaxRhythms];

    std::atomic<int> swapModeAtomic { 0 }; // 0 = OnMasterLoop, 1 = OnRhythmLoop; read by HotSwapStager

    void commitDeferredWork() override;   // hot-swap engine retire + swap commits

    // ProcessorBase MIDI PC hooks — dispatched from drainPendingMidiProgramChanges.
    void applyFullMidiPreset(const juce::File& f) override
        { loadPreset(f); }

    // Base uses this to gate per-slot PCs against the runtime-active rhythm count.
    int getNumActiveChannels() const override
        { return numActiveRhythms.load(std::memory_order_acquire); }

public:
    // Per-slot + full preset directories / extensions. Public so the shared
    // MIDI editor panels (`MidiPresetsPanel` / `MidiFullPresetsPanel`) and the
    // mu-clid editor (preset browser) can read them through a PluginProcessor&.
    juce::File   getPerSlotPresetDir()       const override { return getRhythmsDir(); }
    juce::String getPerSlotPresetExtension() const override { return "muRhythm"; }
    juce::String getFullPresetExtension()    const override { return "muClid"; }

private:

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

    // Reference accessor for UI orchestrators that want to wrap their own
    // multi-write sequence in a `mu_core::ScopedApvtsLoading` guard so the
    // intermediate APVTS state stays invisible to listeners (RhythmPanel's
    // refreshSuffix in particular). Caller is responsible for any post-guard
    // engine resync — see `forceSyncRhythmFromAPVTS` for the canonical pattern.
    std::atomic<bool>& getApvtsLoadingFlag() noexcept { return apvtsLoading; }

    // Public so the insert-algo-change UI flow can re-sync engine state after
    // its guarded multi-write — APVTS holds the new slot values but
    // setParams was suppressed under the guard, so VoiceEngine's pendingParams
    // would otherwise lag a block. Iterates kRhythmParamDefs to populate
    // r.voiceParams from current APVTS, then calls updatePattern + setParams.
    void forceSyncRhythmFromAPVTS(int ri);
private:

    // ── processBlock phases (full + Lite paths) ──────────────────────────
    // processBlock is decomposed into private helpers invoked in order on the
    // audio thread. None allocate or take locks beyond processBlock's own
    // rhythmsLock ScopedTryLock.
    struct BlockTransport { bool playing; double beatPos; };
#if MUCLID_LITE_BUILD
    // Lite MIDI-only path: read playhead / internal transport, publish
    // sequencerPlaying + lastBeatPos.
    BlockTransport computeLiteTransport(int numSamples);
    // Step the sequencer, compute accent velocities, dispatch MIDI triggers,
    // update play-state atomics, and flush midiEngines. Called unconditionally
    // (handles the playing/stopped branch internally).
    void advanceLiteSequencer(int numRhythms, bool playing, double beatPos,
                              juce::MidiBuffer& midi, int numSamples);
#else
    // Scan MIDI clock + program changes, read the playhead / clock / internal
    // transport, reset the wrap detector on a stop->start edge, and publish
    // sequencerPlaying + lastBeatPos.
    BlockTransport deriveTransport(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages);
    // Step the sequencer, fire voice triggers, check hot-swap boundaries, and
    // publish per-rhythm UI play-state. Called only while playing.
    void advanceSequencer(int numRhythms, double beatPos);
    // Modulation pass for one rhythm: snapshot voiceParams, run the matrix,
    // snapshot for the UI live-arc, write modulated values + euclid overrides back.
    void applyRhythmModulation(int r, double beatPos);
    // applyRhythmModulation's phases (all audio thread, under the rhythm's modLock up to phase 3).
    struct StripMod { float pan, effect, delay, reverb; };   // modulated mixer-strip values
    void     seedModulation(int r, const Rhythm& rhythm, const VoiceParams& modParams);
    StripMod applyStripModulation(int r);
    void     publishModSnapshot(int r, const Rhythm& rhythm, const VoiceParams& modParams, const StripMod& stripMod);
    void     writeBackModulation(int r, const Rhythm& rhythm, VoiceParams& modParams);
    // Effective BPM for tempo-synced FX: host playhead > MIDI clock > internal.
    double deriveEffectiveBpm();
    // Gather output buses, run the core mixer/voice render, mix the sample
    // preview, and emit per-rhythm MIDI.
    void renderAudioBuses(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages,
                          int numRhythms, double effectiveBpm);
#endif

    // suspendProcessing() sets a flag but does NOT block until the current
    // processBlock callback finishes. rhythmsLock provides the missing barrier.
    juce::CriticalSection rhythmsLock;

    // Multi-bus output (DAW). Read by isBusesLayoutSupported at host scan-time;
    // persisted to appSettings so it survives across plugin instances.
    std::atomic<bool> multiBusEnabled { true };

    // Note mode state (audio thread writes, message thread reads getMidiNoteMode).
    std::atomic<int>    midiNoteMode    { 0 };   // 0=Free, 1=Note
    std::atomic<int>    midiHeldNotes   { 0 };   // count of currently held MIDI notes (Note mode)
    std::atomic<bool>   noteModePlaying { false };
    std::atomic<double> noteModeBeatPos { 0.0 };

    // Pre-allocated modulation parameter map — reused every block to avoid audio-thread allocation.
    // Keys match ModDest::ids. Values are initialised in constructor and updated each block.
    // keyed by `std::string_view` rather than `std::string`. All write/read sites use
    // `const char*` string literals (`modParamValues["amp.attack"] = …`), which previously
    // constructed a temp std::string per access (~30 × N rhythms × 690 blocks/sec on the
    // audio thread). string_view of a literal is alloc-free; the literal's static storage
    // guarantees the view stays valid for the lifetime of the map entry. ModulationMatrix's
    // `find(a.destinationId)` still works because std::string converts implicitly.
    std::unordered_map<std::string_view, float> modParamValues;
    // modParamValues' value for each ModDest::kTable index, resolved once in the constructor.
    std::array<float*, ModDest::kTableSize> modSlot {};
    float& mv(int destIndex) noexcept { jassert(modSlot[(size_t) destIndex] != nullptr); return *modSlot[(size_t) destIndex]; }

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
    // FX / return / master / channel-strip params route to the shared
    // ProcessorBase::syncGlobalFxParam (mu-core) — see the backlog.
    void pushRhythmToAPVTS(int ri);
    // forceSyncRhythmFromAPVTS lifted to the public section above so UI
    // orchestrators (insert-algo dropdown) can call it after their own
    // apvtsLoading-guarded multi-writes.
    void pushMixerChannelToAPVTS(int idx);
    void swapAPVTSForRhythms(int i, int j);
    void restoreStateFromTree(const juce::ValueTree& s) { presetIO.restoreStateFromTree(s); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    PresetIO       presetIO      { *this };
    HotSwapStager  hotSwapStager { *this };

    friend class PresetIO;
    friend class HotSwapStager;
    friend class SampleLibrary;
    friend class RhythmManager;

    // atomic for safe cross-thread access (audio writes, UI reads + clears).
    // Written in prepareToPlay; read in processBlock. JUCE calls prepareToPlay
    // while the audio thread is suspended by the host, so these fields are never
    // concurrently read and written — no atomic needed.
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
