#pragma once

#include "Modulation/ModulationDestinations.h"   // ModDest::kTableSize (modSlot)
#include <array>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Plugin/ProcessorBase.h"
#include "Sequencer/GeneratorAliases.h"
#include "Audio/VoiceEngine.h"
#include "Audio/MidiOutputEngine.h"
#include "Audio/MidiOutputMapping.h"   // mu-core: velocity + note names for the MIDI output mode
#include "Audio/FX/Slots/FXChain.h"
#include "Audio/MixerEngine.h"
#include "License/LicenseKey.h"       // product: mu-Clid id + filename + public key
#include "MuLimits.h"
#include "Modulation/ModulationSnapshot.h"
#include "Audio/SampleLibrary.h"
#include "Sequencer/RhythmPlayState.h"
#include "Plugin/MidiClockSync.h"   // shared mu-core MIDI-clock slave
#include "Persistence/PresetIO.h"
#include "HotSwapStager.h"

#include <memory>
#include <thread>
#include <vector>
#include <unordered_map>

namespace mu_clid {

class PluginProcessor : public ProcessorBase,
                        private juce::AudioProcessorValueTreeState::Listener
{
public:
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
    juce::String getProductSlug() const override { return "mu-clid"; }   // key in latest.json
    // True for both DAW and standalone:
    //   - Standalone: MIDI clock sync.
    //   - DAW (VST3/CLAP): program-change → preset hot-swap.
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& d) override { presetIO.getStateInformation(d); }
    void setStateInformation(const void* d, int s) override { presetIO.setStateInformation(d, s); notifyHostStateRestored(); }


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

    // The rhythms are ProcessorBase layers: add / delete / reorder / reset are ProcessorBase::addLayer,
    // removeLayer, swapLayers and resetLayer, with the hooks overridden below. Rename is the one edit of
    // its own: it takes the layer lock, because the audio path may read the name.
    void renameRhythm(int index, const juce::String& newName);

    Rhythm& getRhythm    (int index)       { return sequencer.getRhythm(index); }
    const Rhythm& getRhythm(int index) const { return sequencer.getRhythm(index); }
    int     getNumRhythms() const          { return sequencer.getNumRhythms(); }

    // ProcessorBase channel-metadata interface — mu-clid maps "channel" → "rhythm"; the count and
    // getLayer come from the layer container.
    juce::String getLayerName(int idx)      const override
    {
        return (idx >= 0 && idx < getNumRhythms()) ? juce::String(getRhythm(idx).name) : juce::String();
    }
    int          getLayerColourIndex(int idx) const override
    {
        return (idx >= 0 && idx < getNumRhythms()) ? getRhythm(idx).colourIndex : 0;
    }
    void    updatePattern (int index)      { sequencer.updatePattern(index); }

    // Per-rhythm sample paths, load / missing queries, preview player and primary sample folder.
    SampleLibrary samples { *this };

    // Hot-swap staging: stages a rhythm preset for atomic commit at the next loop boundary.
    // If the sequencer is not playing, applies the preset immediately instead.
    void stageRhythmPreset(int ri, const juce::File& f, bool keepIdentity = false)  { presetIO.stageRhythmPreset(ri, f, keepIdentity); }
    void cancelPendingSwap(int ri) override               { hotSwapStager.cancelStagedSwap(ri); }
    bool hasPendingSwap  (int ri) const override         { return hotSwapStager.hasPendingSwap(ri); }
    bool hasPendingFullPreset() const override            { return hotSwapStager.hasPendingFullPreset(); }
    void cancelPendingFullPreset() override               { hotSwapStager.cancelStagedFullPreset(); }
    uint32_t fullStageSerial() const override             { return hotSwapStager.fullStageSerial(); }
    uint32_t slotStageSerial(int ri) const override       { return hotSwapStager.slotStageSerial(ri); }
    int  getMasterLoopSteps() const override              { return sequencer.getMasterLoopSteps(); }
    int  getMasterLoopCurrentStep() const override        { return sequencer.getMasterLoopCurrentStep(); }

    // A rhythm hot-swap commit fires ProcessorBase::onLayerPresetCommitted: the editor refreshes
    // the non-APVTS UI state (name label, sample bar, colour-tinted bits) pushRhythmToApvts can't.

    // ProcessorBase slot-preset API: a rhythm preset into rhythm `ri`.
    void saveLayerPreset(int ri, const juce::String& name) override;
    void loadLayerPreset(int ri, const juce::File& f) override { stageRhythmPreset(ri, f); }
    // The clip hooks: the tree a .muRhythm holds (without file metadata, name, colour) and the tail of a
    // .muRhythm load. Clips reach the sample by path only.
    juce::ValueTree captureLayerPresetTree(int ri) override { return presetIO.captureRhythmPresetTree(ri); }
protected:
    void stageLayerPresetTree(int ri, juce::ValueTree tree, bool keepIdentity) override
        { presetIO.stageRhythmPresetTree(ri, tree, keepIdentity); }
public:
    // Where a staged swap lands: the shared `mstr_swap` parameter, read here on the audio thread from a
    // pointer resolved once. mu-Clid has no bar-line rule, so OnBar behaves as OnLayerLoop.
    mu_hotswap::SwapTiming swapTiming() const noexcept
    {
        const float raw = swapTimingParam != nullptr ? swapTimingParam->load(std::memory_order_relaxed) : 0.0f;
        const auto  t   = mu_hotswap::swapTimingFromIndex(std::isfinite(raw) ? (int) std::lround(raw) : 0);
        return t == mu_hotswap::SwapTiming::OnBar ? mu_hotswap::SwapTiming::OnLayerLoop : t;
    }

    juce::File getRhythmsDir() const;
    juce::File getSamplesDir() const;
    void setContentDir(const juce::File& dir);
    void ensureContentFoldersExist();

    void savePreset(const juce::String& n, const juce::String& d,
                    const juce::String& c, bool e = false) override { presetIO.savePreset(n, d, c, e); }
    void loadPreset(const juce::File& f) override { if (presetIO.loadPreset(f)) publishPresetName(f.getFileNameWithoutExtension()); }
    bool saveFullPresetTo(const juce::File& f) override { return presetIO.saveFullPresetTo(f); }
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


    RhythmSequencer sequencer;
    // Fixed-size arrays so the audio thread never races with a vector reallocation caused by a
    // structural edit on the message thread. The layer count (ProcessorCore::numLayers) is the
    // authoritative count; processBlock reads it atomically once per block, under layersLock.
    std::array<std::unique_ptr<VoiceEngine>, RhythmSequencer::MaxRhythms> voiceEngines;
    std::array<MidiOutputEngine,             RhythmSequencer::MaxRhythms> midiEngines;

    // Note-offs owed by MIDI-mode slots that were torn down inside their 20 ms hold. Written only
    // under layersLock (message thread, audio excluded); the audio thread drains it at the top of
    // the next block under its layersLock try-lock. Fixed size, so nothing allocates.
    static constexpr int kMaxOrphanOffs = 16;
    struct OrphanOff { int note = 0; int channel = 1; };
    std::array<OrphanOff, kMaxOrphanOffs> orphanOffs;
    int numOrphanOffs = 0;
    // Call under layersLock before a slot's MidiOutputEngine is overwritten, moved over or reset.
    void parkPendingMidiOff(int slot)
    {
        int note = 0, chan = 1;
        if (midiEngines[(size_t) slot].takePending(note, chan) && numOrphanOffs < kMaxOrphanOffs)
            orphanOffs[(size_t) numOrphanOffs++] = { note, chan };
    }
    // fxChain and mixerEngine are inherited from ProcessorBase.

    // Stage 34: per-rhythm retired voice engines that continue rendering their
    // in-flight sample / envelope tail after a hot-swap. Step 2 (this commit)
    // wires the storage + render loop + cleanup-flag drain with nothing
    // populating the slots, so behaviour is unchanged. Step 3 wires retire-on-
    // swap, which is what actually populates these. When an engine reports
    // isFullyDrained(), the audio thread store-releases retiredReadyForCleanup;
    // the message thread (runDeferredWork) polls + clears the flag under
    // suspendProcessing and destroys the engine off the RT thread.
    static constexpr int kMaxRetiredEngines = mu_limits::kMaxRetiredVoiceEngines;
    std::array<std::array<std::unique_ptr<VoiceEngine>, kMaxRetiredEngines>,
               RhythmSequencer::MaxRhythms> retiredVoiceEngines;
    std::array<std::array<std::atomic<bool>, kMaxRetiredEngines>,
               RhythmSequencer::MaxRhythms> retiredReadyForCleanup;

    // Play-state atomics: written by audio thread, read by UI at 30 Hz.
    using RhythmPlayState = mu_clid::RhythmPlayState;
    std::array<RhythmPlayState, RhythmSequencer::MaxRhythms> rhythmPlayState;
    std::atomic<float>  beatFraction     { 0.0f }; // fractional position within the current 1/16 step
    std::atomic<bool>   sequencerPlaying { false };
    std::atomic<double> lastBeatPos      { 0.0 };  // most recent beat position (for UI playhead)

    // Snapshot accessor for UI panels (see Modulation/ModulationSnapshot.h for index enum).
    float getModSnapshot(int rhythmIndex, int snapIndex) const noexcept
    {
        return modSnapshot[rhythmIndex][snapIndex].load();
    }

    // UI accessor for the per-rhythm modulated euclid overrides — read by RhythmContent +
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
    std::array<std::atomic<float>, kSnapCount> modSnapshot[RhythmSequencer::MaxRhythms];

    const std::atomic<float>* swapTimingParam { nullptr };   // `mstr_swap`, resolved once in the constructor

    void commitDeferredWork() override;   // hot-swap engine retire + swap commits

    // ProcessorBase MIDI PC hooks — dispatched from drainPendingMidiProgramChanges.
    void applyFullMidiPreset(const juce::File& f) override
        { loadPreset(f); }

public:
    // Per-slot + full preset directories / extensions. Public so the shared
    // MIDI editor panels (`MidiPresetsOverlay` / `MidiFullPresetsOverlay`) and the
    // mu-clid editor (preset browser) can read them through a PluginProcessor&.
    juce::File   getLayerPresetDir()       const override { return getRhythmsDir(); }
    juce::String getLayerPresetExtension() const override { return "muRhythm"; }
    juce::String getFullPresetExtension()    const override { return "muClid"; }

private:

    // now atomic — listeners can fire on the audio thread when a DAW runs
    // host automation, so the cross-thread read in the rhythm-parameter drain needs proper
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
    // intermediate APVTS state stays invisible to listeners (RhythmContent's
    // refreshSuffix in particular). Caller is responsible for any post-guard
    // engine resync — see `forceSyncRhythmFromApvts` for the canonical pattern.
    std::atomic<bool>& getApvtsLoadingFlag() noexcept { return apvtsLoading; }

    // Public so the insert-algo-change UI flow can re-sync engine state after
    // its guarded multi-write — APVTS holds the new slot values but
    // setParams was suppressed under the guard, so VoiceEngine's pendingParams
    // would otherwise lag a block. Iterates kRhythmParamDefs to populate
    // r.voiceParams from current APVTS, then calls updatePattern + setParams.
    void forceSyncRhythmFromApvts(int ri);
private:

    // ── processBlock phases ──────────────────────────────────────────────
    // processBlock is decomposed into private helpers invoked in order on the
    // audio thread. None allocate or take locks beyond processBlock's own
    // layersLock ScopedTryLock.
    struct BlockTransport { bool playing; double beatPos; double bpm = 120.0; };   // bpm = the block's tempo (host > MIDI clock ticks > BPM field)
    // Scan MIDI clock + program changes, read the playhead / clock / internal
    // transport, reset the wrap detector on a stop->start edge, and publish
    // sequencerPlaying + lastBeatPos.
    BlockTransport deriveTransport(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages);
    // Step the sequencer, fire voice triggers (or MIDI notes for rhythms in MIDI output mode, into
    // midiMessages), check hot-swap boundaries, and
    // publish per-rhythm UI play-state. Called only while playing.
    void advanceSequencer(int numRhythms, double beatPos, juce::MidiBuffer& midiMessages);
    // Modulation pass for one rhythm: snapshot voiceParams, run the matrix,
    // snapshot for the UI live-arc, write modulated values + euclid overrides back.
    void applyRhythmModulation(int r, double beatPos);
    // applyRhythmModulation's phases (all audio thread, under the rhythm's modLock up to phase 3).
    struct StripMod { float pan, effect, delay, reverb; };   // modulated mixer-strip values
    void     seedModulation(int r, const Rhythm& rhythm, const VoiceParams& modParams);
    StripMod applyStripModulation(int r);
    void     publishModSnapshot(int r, const Rhythm& rhythm, const VoiceParams& modParams, const StripMod& stripMod);
    void     writeBackModulation(int r, const Rhythm& rhythm, VoiceParams& modParams);
    // Gather output buses, run the core mixer/voice render, mix the sample
    // preview, and emit per-rhythm MIDI.
    void renderAudioBuses(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages,
                          int numRhythms, double effectiveBpm);

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
    std::array<EuclidOverrides, RhythmSequencer::MaxRhythms> lastEuclidOverrides;
    // Previous block's overrides per rhythm — Stage B compares against this to skip
    // pattern recomputes when nothing crossed an integer boundary.
    std::array<EuclidOverrides, RhythmSequencer::MaxRhythms> prevEuclidOverrides;
    // tracks whether the rhythm's modulation matrix had any assignments LAST block.
    // The modulation pass is gated on (matrix non-empty || transition-to-empty), so we
    // skip the seed + matrix.process + write-back work for never-modulated rhythms but
    // still run one final reset pass after assignment removal to flush lastEuclidOverrides
    // back to base values, which Stage B then picks up via change-detection.
    std::array<bool, RhythmSequencer::MaxRhythms> prevMatrixHadAssignments {};

    void parameterChanged(const juce::String& parameterID, float newValue) override;

    // Rhythm parameter changes, queued from any thread and applied on the message thread. A host can
    // call parameterChanged on the audio thread (VST3 automation), where the apply (Rhythm field
    // writes, a pattern rebuild that allocates, the voice-engine hand-over) must not run. The
    // listener therefore does only atomic stores: one dirty flag per (rhythm, suffix of
    // kRhythmParamDefs) plus one per rhythm so a drain skips quiet rhythms. The value is not queued:
    // the drain reads the parameter's own atomic, so it can never apply something older than the
    // parameter, and a burst on one suffix coalesces into one apply. On the message thread the apply
    // happens at once (a knob drag, a preset load); otherwise the shared deferred-work poll
    // (commitDeferredWork, 100 Hz) does it, up to about 10 ms later. Only the message thread writes
    // the Rhythm fields and rebuilds patterns.
    void markRhythmParamDirty(int ri, const char* suffix) noexcept;
    int  findRhythmParamIndex(const char* suffix) const noexcept;
    bool isMessageThread() const noexcept;
    // Apply every queued change: the message thread's poll, or an offline render's processBlock
    // (which passes true to wait for a concurrent apply instead of leaving it).
    bool servicePendingRhythmParams(bool waitForOtherApply = false);   // false: another thread held the apply and was not waited out
    std::atomic_flag applyingRhythmParams = ATOMIC_FLAG_INIT;   // held while one thread applies
    void applyPendingRhythmParams(int ri);        // message thread: one rhythm
    void dropPendingRhythmParams(int ri) noexcept;

    std::array<std::array<std::atomic<bool>, kRhythmParamCount>, RhythmSequencer::MaxRhythms> pendingDirty {};
    std::array<std::atomic<bool>, RhythmSequencer::MaxRhythms> rhythmHasPending {};

    // Per (rhythm, kRhythmParamDefs entry), the parameter's raw value (null if it has none); resolved
    // once in the constructor so the drain does no lookup by id.
    std::array<std::array<std::atomic<float>*, kRhythmParamCount>, RhythmSequencer::MaxRhythms> rawParams {};

    // Suffix to index in kRhythmParamDefs, sorted by suffix, for the listener's lookup by binary
    // search (no String, no allocation). Built in the constructor and never changed.
    struct SuffixIndex { const char* suffix; int index; };
    std::array<SuffixIndex, kRhythmParamCount> suffixIndex {};
    int suffixIndexCount = 0;

    // Where no MessageManager exists (a headless test), the thread that built the processor.
    std::thread::id ownerThread { std::this_thread::get_id() };

    // FX / return / master / channel-strip params route to the shared
    // ProcessorBase::syncGlobalFxParam (mu-core) — see the backlog.
    void pushRhythmToApvts(int ri);
    // forceSyncRhythmFromApvts lifted to the public section above so UI
    // orchestrators (insert-algo dropdown) can call it after their own
    // apvtsLoading-guarded multi-writes.
    void pushMixerChannelToApvts(int idx);
    // Sets the sequencer's live rhythm count; a dropped rhythm is reset so a later grow starts from a default one.
    void setRhythmCount(int n);

    // ── Layer container (ProcessorBase) ──────────────────────────────────────
    // The engines, tails, play state and sample paths stay here, keyed by slot; the hooks below move
    // them with the rhythms. All of them run on the message thread, processing suspended.
    bool canEditLayers() const override { return true; }
    bool layerHoldsParamTruth() const override { return true; }
    // A default rhythm for `slot`. For a slot at or above the live count it also readies that slot's
    // voice and MIDI engines (built here, outside every lock), so the publish only moves pointers.
    std::unique_ptr<Layer> createLayer(int slot) override;
    // Under layersLock: the sequencer's count and rhythm bindings follow the layer set.
    void layersMoved(int firstIndex, int lastIndex) override;
    // Under layersLock, before layersMoved: moves the per-slot audio state with the rhythms.
    void layersReordered(LayerEdit kind, int a, int b) override;
    // After the unlock: pushes the slot's rhythm and strip into the parameters, rebuilds a new or
    // reset rhythm's pattern and engine parameters, and releases what the edit parked.
    void syncSlotParams(int slot) override;
    // After an add: the default rhythm preset, as the editor's Add did.
    void layerAdded(int index) override { applyDefaultRhythm(index); }

    std::unique_ptr<Layer> makeRhythm(int slot) const;
    void prepareSlotEngines(int slot);
    void resetPlayState(int slot);
    void releaseParkedEngines();

    // What the audio state moved out of a removed slot waits in until the unlock: its voice engine
    // and the slot's retired tails. Never constructed or freed under layersLock.
    static constexpr int kParkedEngines = 1 + kMaxRetiredEngines;
    std::array<std::unique_ptr<VoiceEngine>, kParkedEngines> parkedEngines;
    // Edit bookkeeping between the locked hooks and syncSlotParams: the slot whose rhythm is new
    // (add / reset) and so needs its pattern and engine parameters rebuilt, and, during a remove or
    // swap, the last slot syncSlotParams will be given.
    int rebuildSlot     = -1;
    int reorderLastSlot = -1;
    void restoreStateFromTree(const juce::ValueTree& s) { presetIO.restoreStateFromTree(s); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    PresetIO       presetIO      { *this };
    HotSwapStager  hotSwapStager { *this };

    friend class PresetIO;
    friend class HotSwapStager;
    friend class SampleLibrary;
    friend struct ClipCommitProbe;         // tests drive the boundary scan and commit of a staged launch
    friend struct ModulationMemoryProbe;   // tests read the per-slot modulation change-detection state

    // atomic for safe cross-thread access (audio writes, UI reads + clears).
    // Written in prepareToPlay; read in processBlock. JUCE calls prepareToPlay
    // while the audio thread is suspended by the host, so these fields are never
    // concurrently read and written — no atomic needed.
    double currentSampleRate = 44100.0;
    int    currentBlockSize  = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace mu_clid
