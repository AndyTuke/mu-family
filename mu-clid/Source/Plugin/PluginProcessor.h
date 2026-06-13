#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Plugin/ProcessorBase.h"
#include "Sequencer/SequencerEngine.h"
#include "Audio/VoiceEngine.h"
#include "Audio/MidiOutputEngine.h"
#include "Audio/FX/Slots/FXChain.h"
#include "Audio/MixerEngine.h"
#include "License/LicenseManager.h"   // mu-core: shared offline-license verifier
#include "License/LicenseKey.h"       // product: mu-Clid id + filename + public key
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

    // `apvts` lives on ProcessorBase (mu-core) — every mu-family plugin shares
    // one. Layout is supplied via createParameterLayout() when the base ctor
    // runs. PluginProcessor's own members below may reference apvts; they
    // initialize after the base, so the reference is safe.
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

    // internalPlaying / internalBeatPos are touched by both audio (write) and
    // message thread (read + clear). std::atomic<> with relaxed ordering — no other
    // memory is published through them, so relaxed is sufficient.
    void   toggleInternalPlay() override
    {
        const bool nowPlaying = !internalPlaying.load(std::memory_order_relaxed);
        internalPlaying.store(nowPlaying, std::memory_order_relaxed);
        if (!nowPlaying) internalBeatPos.store(0.0, std::memory_order_relaxed);
    }
    bool   isInternalPlaying()   const override { return internalPlaying.load(std::memory_order_relaxed); }
    void   setInternalBpm(double bpm)  override { internalBpm.store(juce::jlimit(20.0, 300.0, bpm), std::memory_order_relaxed); }
    double getInternalBpm()      const override { return internalBpm.load(std::memory_order_relaxed); }
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

    // UI scale (Medium=1.0, Large=1.25). Persisted via appSettings. Editor reads
    // at ctor time and applies to mu_ui::scale BEFORE constructing children so
    // ctor-time fonts pick up the right size on a fresh open. Runtime
    // changes go via setUiScale → onUiScaleChanged callback so the editor can
    // resize + relayout live (with a "reopen for fonts" hint near the picker).
    // kUiScaleMedium / kUiScaleLarge constants + onUiScaleChanged callback live
    // on ProcessorBase; mu-clid's override persists through appSettings before
    // delegating to the base.
    void  setUiScale(float scale) override;
    float getUiScale() const noexcept override { return uiScale; }

    static constexpr int kMasterBusIndex   = 0;
    static constexpr int kFirstDirectOutBus = 1;   // Out 1 = bus 1 ... Out 8 = bus 8
    static constexpr int kFXReturnsBusIndex = 9;
    static constexpr int kTotalBuses        = 10;

    static constexpr int kAutomatedRhythms  = mu_limits::kMaxAutomatedChannels;

    // MIDI clock sync (standalone only).
    void   setMidiSyncEnabled(bool on);
    bool   getMidiSyncEnabled()  const override { return midiClockSync.isEnabled(); }
    void   setMidiSyncMessages(int mode);
    int    getMidiSyncMessages() const override { return midiClockSync.getMessages(); }
    bool   isMidiClockPlaying()  const override { return midiClockSync.isPlaying(); }
    double getMidiClockBpm()     const override { return midiClockSync.getBpm(); }

    // MIDI Note mode (plugin only). 0=Free (host transport drives play), 1=Note (Note On/Off drives play).
    void setMidiNoteMode(int mode);
    int  getMidiNoteMode() const { return midiNoteMode.load(std::memory_order_relaxed); }

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
    bool hasPendingFullPreset() const override            { return hotSwapStager.hasPendingFullPreset(); }
    int  getMasterLoopSteps() const                       { return sequencer.getMasterLoopSteps(); }

    // Headless/offline render helper. The render loop drives processBlock directly
    // and never returns to JUCE's message dispatch loop, so triggerAsyncUpdate()
    // would never be serviced. Call this after each processBlock so deferred work
    // (staged preset-swap commit, MIDI program-change handling) runs synchronously
    // on the calling thread. No-op when nothing is pending.
    void flushPendingAsyncUpdates() { handleUpdateNowIfNeeded(); }

    // fired (on the message thread, from handleAsyncUpdate) after a hot-swap
    // commit finishes. The editor uses this to refresh non-APVTS UI state — name
    // label, sample bar, colour-tinted bits — that pushRhythmToAPVTS doesn't cover
    // because those fields aren't APVTS parameters. Editor MUST clear this in its
    // destructor: the processor can outlive the editor (DAW close-window-keep-
    // plugin), and a swap commit firing into a destroyed editor is a UAF.
    // onPresetSwapCommitted + onLoadError + onUiScaleChanged live on ProcessorBase
    // since every plugin's shell handles them identically; onRhythmHotSwapCommitted
    // stays here because "rhythm" is mu-clid-specific.
    std::function<void(int rhythmIndex)> onRhythmHotSwapCommitted;
    void setSwapMode(SwapMode m) { swapModeAtomic.store((int)m, std::memory_order_relaxed); }
    SwapMode getSwapMode() const { return static_cast<SwapMode>(swapModeAtomic.load(std::memory_order_relaxed)); }

    juce::File getContentDir() const override;
    juce::File getPresetsDir() const override;
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

    // License — checked once at startup; result is immutable thereafter. Release builds
    // require a valid license (no license → Demo); Debug builds (testers) run unlocked.
    mu_core::LicenseManager::Info licenseInfo;
    bool isLicensed() const override
    {
       #if MUCLID_LITE_BUILD
        return true;   // Lite is always licensed — no activation, no demo caps.
       #elif MUFAMILY_REQUIRE_LICENSE
        // Licensed if EITHER the offline signed .lic verifies OR the machine is online-activated.
        return licenseInfo.status == mu_core::LicenseStatus::Licensed || isOnlineActivated();
       #else
        return true;   // Debug / tester build — full features, no license required
       #endif
    }
    // Activation record filename (online path), next to the offline .lic in the content dir.
    static constexpr const char* kActivationFilename = "muclid.activation";
    // Demo limits the unlicensed editor to a single rhythm.
    int demoMaxChannels() const override { return 1; }

    void savePreset(const juce::String& n, const juce::String& d,
                    const juce::String& c, bool e = false) override { presetIO.savePreset(n, d, c, e); }
    void loadPreset(const juce::File& f) override { presetIO.loadPreset(f); }
    void saveRhythmPresetToFile(int ri, const juce::File& dest,
                                bool emb = false, const juce::String& cat = {},
                                const juce::String& desc = {})
                                { presetIO.saveRhythmPresetToFile(ri, dest, emb, cat, desc); }

    // Shared preset category list.
    juce::StringArray loadCategoryList() const override  { return presetIO.loadCategoryList(); }
    void ensureCategoryInList(const juce::String& c) override { presetIO.ensureCategoryInList(c); }
    bool applyRhythmPreset(const juce::File& f, int ri) { return presetIO.applyRhythmPreset(f, ri); }
    bool applyDefaultRhythm(int ri)              { return presetIO.applyDefaultRhythm(ri); }
    void loadDefaultPreset()                     { presetIO.loadDefaultPreset(); }

    // Set true before constructing a PluginProcessor to skip the ctor's
    // automatic `loadDefaultPreset` call. Used by the headless render path
    // (Source/Plugin/RenderMode.cpp) so listening tests aren't perturbed by
    // whatever the user has saved as their personal `_default.muClid`.
    inline static bool skipAutoLoadDefault = false;

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
        std::atomic<int>  hitCount      { 0 };     // monotonic counter — audio thread increments per hit, UI tracks lastSeen (avoids one-shot-flag race between RhythmCircle + SidebarItem)
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

    // UI accessor for the per-rhythm modulated euclid overrides — read by RhythmPanel +
    // SidebarItem on a timer so the visual circles reflect modulation of hits/rotate/etc.
    //. Returns a struct copy; fields are integer-rounded by the audio thread so
    // torn reads are benign for visual display. Pre-fix the audio thread only
    // wrote `lastEuclidOverrides[r]` when the modulation pass actually ran, leaving the
    // overrides at C++-default zeros for any rhythm with no assignments — so the default
    // rhythm at startup drew no hits. Fall back to the rhythm's base gen values when no
    // modulation pass has touched the entry so `HitGenerator::getStepTypes(ov)` yields
    // the rhythm's actual pattern for unmodulated cases.
    EuclidOverrides getModulatedEuclidOverrides(int rhythmIdx) const noexcept
    {
        if (rhythmIdx < 0 || rhythmIdx >= (int) lastEuclidOverrides.size()
                          || rhythmIdx >= sequencer.getNumRhythms())
            return {};
        const Rhythm& r = sequencer.getRhythm(rhythmIdx);
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
        return lastEuclidOverrides[(size_t) rhythmIdx];
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

    void handleAsyncUpdate() override;

    // ProcessorBase MIDI PC hooks — dispatched from drainPendingMidiProgramChanges.
    void applyMidiPresetSlot(int slot, const juce::File& f) override
        { stageRhythmPreset(slot, f); }
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
    juce::File   getFullPresetDir()          const override { return getPresetsDir(); }
    juce::String getFullPresetExtension()    const override { return "muClid"; }

private:

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

    // uiScale storage lives on ProcessorBase (shared family-wide). mu-clid's
    // setUiScale override persists through appSettings before delegating up.

    MidiClockSync midiClockSync;

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
    std::atomic<double> internalBpm       { 120.0 };  // written by message thread, read by audio thread
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
