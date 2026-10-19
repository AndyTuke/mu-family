#pragma once

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
#include "Persistence/RhythmPresetTree.h"   // the Rhythm type's saved form (PreparedRhythm, RhythmPresetContext)
#include "UI/RhythmHost.h"   // what the Rhythm layer's UI reads from its host
#include "Sequencer/SampleRhythm.h"   // mu-Clid's layer type
#include "HotSwapStager.h"

#include <memory>
#include <thread>
#include <vector>
#include <unordered_map>

namespace mu_clid {

class PluginProcessor : public ProcessorBase,
                        public RhythmHost,
                        public SampleSlotHost,
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

    // The host session (PluginProcessor_HostState.cpp): the .muClid tree plus the parameters it doesn't carry.
    void getStateInformation(juce::MemoryBlock& d) override;
    void setStateInformation(const void* d, int s) override { restoreHostState(d, s); notifyHostStateRestored(); }


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

    Rhythm& getRhythm    (int index)       { return layerAs<Rhythm>(index); }
    const Rhythm& getRhythm(int index) const { return layerAs<Rhythm>(index); }
    int     getNumRhythms() const          { return liveRhythms.load(std::memory_order_relaxed); }

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
    void    updatePattern (int index)      { updateRhythmPattern(index); }

    // Per-rhythm sample paths, load / missing queries, preview player and primary sample folder.
    SampleLibrary samples { *this };

    // Hot-swap staging: stages a rhythm preset for atomic commit at the next loop boundary.
    // If the sequencer is not playing, applies the preset immediately instead.
    // keepIdentity: the slot keeps its name and colour (a settings reset such as the default rhythm).
    void stageRhythmPreset(int ri, const juce::File& f, bool keepIdentity = false);
    void cancelPendingSwap(int ri) override               { hotSwapStager.cancelStagedSwap(ri); }
    bool hasPendingSwap  (int ri) const override         { return hotSwapStager.hasPendingSwap(ri); }
    bool hasPendingFullPreset() const override            { return hotSwapStager.hasPendingFullPreset(); }
    void cancelPendingFullPreset() override               { hotSwapStager.cancelStagedFullPreset(); }
    uint32_t fullStageSerial() const override             { return hotSwapStager.fullStageSerial(); }
    uint32_t slotStageSerial(int ri) const override       { return hotSwapStager.slotStageSerial(ri); }
    int  getMasterLoopSteps() const override              { return stepClock.getMasterLoopSteps(); }
    int  getMasterLoopCurrentStep() const override        { return stepClock.getMasterLoopCurrentStep(); }

    // A rhythm hot-swap commit fires ProcessorBase::onLayerPresetCommitted: the editor refreshes
    // the non-APVTS UI state (name label, sample bar, colour-tinted bits) pushRhythmToApvts can't.

    // ProcessorBase slot-preset API: a rhythm preset into rhythm `ri`.
    void saveLayerPreset(int ri, const juce::String& name) override;
    void loadLayerPreset(int ri, const juce::File& f) override { stageRhythmPreset(ri, f); }
    // The clip hooks: the tree a .muRhythm holds (without file metadata, name, colour) and the tail of a
    // .muRhythm load. Clips reach the sample by path only.
    juce::ValueTree captureLayerPresetTree(int ri) override;
protected:
    // Everything a .muRhythm load does after the parse (file or clip): installs at once when stopped, else commits
    // at the rhythm's boundary. Callers go through ProcessorBase::stageLayerPreset.
    void stageLayerPresetTree(int ri, juce::ValueTree tree, bool keepIdentity) override;
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

    // Preset orchestration (PluginProcessor_Preset.cpp).
    void savePreset(const juce::String& n, const juce::String& d,
                    const juce::String& c, bool e = false) override;
    void loadPreset(const juce::File& f) override { if (loadPresetFile(f)) publishPresetName(f.getFileNameWithoutExtension()); }
    // A full preset or host-state file: a .muClid is prepared off the audio thread and committed at the next loop
    // point when playing, at once when stopped; another state root of this product restores as legacy host state.
    // False when nothing could be read (nothing was loaded or staged).
    bool loadPresetFile(const juce::File& f);
    bool saveFullPresetTo(const juce::File& f) override;   // headless render --save-preset
    void saveRhythmPresetToFile(int ri, const juce::File& dest,
                                bool emb = false, const juce::String& cat = {},
                                const juce::String& desc = {});

    // Shared preset category list.
    juce::StringArray loadCategoryList() const override;
    void ensureCategoryInList(const juce::String& c) override;
    // A .muRhythm into slot `ri` at once, whatever the transport is doing.
    bool applyRhythmPreset(const juce::File& f, int ri, bool keepIdentity = false);
    bool applyDefaultRhythm(int ri);
    // _default.muClid, falling back to a single-rhythm _default.muRhythm.
    void loadDefaultPreset() override;


    // The rhythms' own step clocks: the host half here (it holds the master-loop length), a RhythmCursor in each
    // SampleRhythm. They are handed every pattern, swap start, loop length and restart, and decide each block's hits.
    RhythmStepClock stepClock;
    static constexpr int kStepCap = HitGenerator::kMaxSteps;   // the Euclid step cap every pattern is built with

    // Rebuilds slot `index`'s pattern in the rhythm's cursor (message thread).
    void updateRhythmPattern(int index)
    {
        if (index >= 0 && index < mu_limits::kMaxLayers)
            sampleRhythm(index).cursor.updatePattern(sampleRhythm(index), kStepCap);
    }
    // Where slot r's pattern last started a pass, and the step counter's run id (audio thread: the swap stager
    // records both at a boundary).
    int      swapBoundaryEff(int r) const noexcept { return sampleRhythm(r).cursor.boundaryEffective(); }
    uint32_t swapFrameEpoch() const noexcept       { return stepClock.currentEpoch(); }
    // Slot `slot`'s voice engine: SampleRhythm::voice (E5.5d), so it moves with its layer. Null in a slot that
    // is not live. Each rhythm's MidiOutputEngine is SampleRhythm::midi (E5.5c); the engines its swaps retired
    // sound out in the shared RetireBin (retireEngine).
    std::unique_ptr<VoiceEngine>&       voiceEngine(int slot) noexcept       { return sampleRhythm(slot).voice; }
    const std::unique_ptr<VoiceEngine>& voiceEngine(int slot) const noexcept { return sampleRhythm(slot).voice; }

    // Note-offs owed by MIDI-mode rhythms that were removed or dropped inside their 20 ms hold: a removed
    // layer hands its owed note-off over in layerRemoved. Written only under layersLock (message thread,
    // audio excluded); the audio thread drains it at the top of the next block under its layersLock
    // try-lock. Fixed size, so nothing allocates; an owed note-off past the 16th is dropped.
    static constexpr int kMaxOrphanOffs = 16;
    struct OrphanOff { int note = 0; int channel = 1; };
    std::array<OrphanOff, kMaxOrphanOffs> orphanOffs;
    int numOrphanOffs = 0;
    // Call under layersLock before a MidiOutputEngine is discarded or reset: its owed note-off is kept.
    void parkPendingMidiOff(MidiOutputEngine& engine)
    {
        int note = 0, chan = 1;
        if (! engine.takePending(note, chan)) return;
        jassert(numOrphanOffs < kMaxOrphanOffs);   // more than 16 owed note-offs in one block: the rest are dropped
        if (numOrphanOffs < kMaxOrphanOffs)
            orphanOffs[(size_t) numOrphanOffs++] = { note, chan };
    }
    // fxChain and mixerEngine are inherited from ProcessorBase.

    // A hot-swap's outgoing voice engine keeps rendering its in-flight sample / envelope tail in slot `slot`'s
    // channel from the shared RetireBin (an EngineTail, E5.5e) until it reports drained, at most 30 s; the bin frees
    // it on the message thread. Call under layersLock (the audio thread is then outside the bin): it starts with the
    // next rendered block; a full row evicts its entry 0, whose engine is freed later by a service pass.
    void retireEngine(int slot, std::unique_ptr<VoiceEngine> engine);

    // Tests only (message thread, not processing): slot `slot`'s retire-bin entry `entry`: its engine while the
    // entry still sounds (Parked or Draining; null otherwise) and its state.
    const VoiceEngine*        retiredEngineForTest(int slot, int entry) const noexcept;
    mu_core::RetireBin::State retiredStateForTest(int slot, int entry) const noexcept { return retireBin.stateOf(slot, entry); }
    int                       retiredCountForTest(int slot) const noexcept;
    // Tests only: retires `engine` into slot `slot`'s row now, as a swap's install does (takes the layer lock).
    void retireEngineForTest(int slot, std::unique_ptr<VoiceEngine> engine)
    {
        const juce::ScopedLock sl(layersLock);
        retireEngine(slot, std::move(engine));
    }

    // Play-state atomics: written by audio thread, read by UI at 30 Hz. Each rhythm's own play state is its layer's
    // (SampleRhythm::playState), looked up through getRhythmPlayState on every read.
    std::atomic<bool>   sequencerPlaying { false };
    std::atomic<double> lastBeatPos      { 0.0 };  // most recent beat position (for UI playhead)

    // Snapshot accessor for UI panels (see Modulation/ModulationSnapshot.h for index enum).
    float getModSnapshot(int rhythmIndex, int snapIndex) const noexcept
    {
        return sampleRhythm(rhythmIndex).modSnapshot[(size_t) snapIndex].load();
    }

    // Slot `slot`'s layer as mu-Clid's layer type (every slot holds one for the processor's lifetime).
    SampleRhythm&       sampleRhythm(int slot) noexcept       { return layerAs<SampleRhythm>(slot); }
    const SampleRhythm& sampleRhythm(int slot) const noexcept { return layerAs<SampleRhythm>(slot); }

    // UI accessor for the per-rhythm modulated euclid overrides — read by RhythmContent +
    // SidebarItem on a timer so the visual circles reflect modulation of hits/rotate/etc.
    // A struct copy (see SampleRhythm::modulatedEuclidOverrides); empty for a slot that is not live.
    EuclidOverrides getModulatedEuclidOverrides(int rhythmIndex) const noexcept
    {
        if (rhythmIndex < 0 || rhythmIndex >= mu_limits::kMaxLayers
                          || rhythmIndex >= getNumRhythms())
            return {};
        return sampleRhythm(rhythmIndex).modulatedEuclidOverrides();
    }

    // GR-meter pointer for the per-voice compressor/limiter insert UI knob.
    // Returns nullptr when the rhythm index is out of range or the engine is null.
    std::atomic<float>* getInsertGRReductionPtr(int ri)
    {
        if (ri < 0 || ri >= getNumRhythms() || ! voiceEngine(ri)) return nullptr;
        return &voiceEngine(ri)->insertProc.grReduction;
    }

    // ── RhythmHost: what RhythmContent and the sidebar's mini circle read (message thread) ─────────
    juce::AudioProcessorValueTreeState& getRhythmParameters() noexcept override { return apvts; }
    int           getRhythmCount() const noexcept override                    { return getNumRhythms(); }
    Rhythm&       getRhythmLayer(int layer) noexcept override                 { return getRhythm(layer); }
    const Rhythm& getRhythmLayer(int layer) const noexcept override           { return getRhythm(layer); }
    bool                     isRhythmTransportPlaying() const noexcept override { return sequencerPlaying.load(); }
    const std::atomic<bool>& getRhythmTransportFlag() const noexcept override   { return sequencerPlaying; }
    RhythmPlayState*         getRhythmPlayState(int layer) noexcept override
    {
        return getLayer(layer) != nullptr ? &sampleRhythm(layer).playState : nullptr;
    }
    float getRhythmModLive(int layer, int snap) const noexcept override         { return getModSnapshot(layer, snap); }
    EuclidOverrides getRhythmEuclidOverrides(int layer) const noexcept override { return getModulatedEuclidOverrides(layer); }
    const std::atomic<float>* getRhythmInsertGainReduction(int layer) override  { return getInsertGRReductionPtr(layer); }
    bool               isRhythmParamsLoading() const noexcept override          { return isApvtsLoading(); }
    std::atomic<bool>& getRhythmParamsLoadingFlag() noexcept override           { return getApvtsLoadingFlag(); }
    void               syncRhythmFromParameters(int layer) override             { forceSyncRhythmFromApvts(layer); }
    void renameRhythmLayer(int layer, const juce::String& newName) override     { renameRhythm(layer, newName); }
    void resetRhythmLayer(int layer) override                                   { resetLayer(layer); }
    int  getRhythmClipCount(int layer) const override                           { return clipCount(layer); }
    juce::File getRhythmPresetsDir() const override                             { return getRhythmsDir(); }
    void saveRhythmPresetFile(int layer, const juce::File& dest, bool embedSample,
                              const juce::String& category, const juce::String& description) override
        { saveRhythmPresetToFile(layer, dest, embedSample, category, description); }
    void stageRhythmPresetFile(int layer, const juce::File& file) override      { stageRhythmPreset(layer, file); }
    juce::StringArray getRhythmPresetCategories() const override                { return loadCategoryList(); }
    void addRhythmPresetCategory(const juce::String& category) override        { ensureCategoryInList(category); }
    SampleLibrary& getRhythmSamples() noexcept override                         { return samples; }
    juce::File     getRhythmSamplesDir() const override                         { return getSamplesDir(); }

    // ── SampleSlotHost: the engines and the settings file the sample library uses (message thread) ──
    int getSampleSlotCount() const noexcept override                         { return numLayers.load(std::memory_order_acquire); }
    VoiceEngine*       getSampleSlotEngine(int slot) noexcept override       { return voiceEngine(slot).get(); }
    const VoiceEngine* getSampleSlotEngine(int slot) const noexcept override { return voiceEngine(slot).get(); }
    juce::PropertiesFile* getSampleSettingsFile() const noexcept override    { return appSettings.get(); }

private:
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
    // audio thread. None allocate or take locks beyond the driver's layersLock try-lock.
    struct BlockTransport { bool playing; double beatPos; double bpm = 120.0; };   // bpm = the block's tempo (host > MIDI clock ticks > BPM field)
    // Scan MIDI clock + program changes, read the playhead / clock / internal
    // transport, reset the wrap detector on a stop->start edge, and publish
    // sequencerPlaying + lastBeatPos.
    BlockTransport deriveTransport(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages);
    // The block's hits: the host clock's frame, each live rhythm's cursor walk, gathered as a BlockResult;
    // `steps` gets each rhythm's step, accent step and length.
    struct CursorSteps { int step = -1, accentStep = 0, length = 0; };
    BlockResult advanceCursors(int numRhythms, double beatPos, std::array<CursorSteps, mu_limits::kMaxLayers>& steps);
    // ── The render: the shared layer driver ──────────────────────────────────────────────────────────
    // The host's buses, the block's transport, then the shared driver (ProcessorBase::renderAndMix): the
    // rhythms render themselves (SampleRhythm::render) and the mixer runs, in chunks of the prepared size.
    void renderThroughLayers(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages, const BlockTransport& transport);
    // The driver's per-block hooks (rendered blocks only, under the layer lock): the start sends the owed
    // note-offs, finds each rhythm's hit with the host step clock and the rhythm cursors (MIDI hits go out
    // now, sample hits wait for the rhythm's render), checks the swap boundaries, publishes the play state and
    // hands each rhythm its block inputs; the end mixes the sample preview and counts down the MIDI note-offs.
    void layersBlockStarting(const mu_core::LayerView& view, const mu_core::BlockTransport& t, juce::MidiBuffer& midi) override;
    void layersBlockRendered(const mu_core::LayerView& view, juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    void sendOrphanOffs(juce::MidiBuffer& midiMessages);

    // Multi-bus output (DAW). Read by isBusesLayoutSupported at host scan-time;
    // persisted to appSettings so it survives across plugin instances.
    std::atomic<bool> multiBusEnabled { true };

    // Note mode state (audio thread writes, message thread reads getMidiNoteMode).
    std::atomic<int>    midiNoteMode    { 0 };   // 0=Free, 1=Note
    std::atomic<int>    midiHeldNotes   { 0 };   // count of currently held MIDI notes (Note mode)
    std::atomic<bool>   noteModePlaying { false };
    std::atomic<double> noteModeBeatPos { 0.0 };

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

    std::array<std::array<std::atomic<bool>, kRhythmParamCount>, mu_limits::kMaxLayers> pendingDirty {};
    std::array<std::atomic<bool>, mu_limits::kMaxLayers> rhythmHasPending {};

    // Per (rhythm, kRhythmParamDefs entry), the parameter's raw value (null if it has none); resolved
    // once in the constructor so the drain does no lookup by id.
    std::array<std::array<std::atomic<float>*, kRhythmParamCount>, mu_limits::kMaxLayers> rawParams {};

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
    // Sets the live rhythm count; a dropped rhythm is reset so a later grow starts from a default one.
    void setRhythmCount(int n);
    // The live rhythm count; a cursor whose slot becomes live or stops being live is cleared (message thread,
    // layer lock held or not yet processing).
    void setLiveRhythms(int n);

    // ── Layer container (ProcessorBase) ──────────────────────────────────────
    // The engines and tails are the layers' and move with them; the play state and sample paths stay here,
    // keyed by slot, and the hooks below move them with the rhythms. All of them run on the message thread,
    // processing suspended.
    bool canEditLayers() const override { return true; }
    bool layerHoldsParamTruth() const override { return true; }
    // A default rhythm for `slot`. For a slot at or above the live count it also readies that slot's
    // voice and MIDI engines (built here, outside every lock), so the publish only moves pointers.
    std::unique_ptr<Layer> createLayer(int slot) override;
    // Under layersLock: the live rhythm count follows the layer set.
    void layersMoved(int firstIndex, int lastIndex) override;
    // Under layersLock, before layersMoved: moves the per-slot audio state with the rhythms.
    void layersReordered(LayerEdit kind, int a, int b) override;
    // Under layersLock, before layersReordered: the removed rhythm's owed MIDI note-off is kept.
    void layerRemoved(Layer& removed, int index) override;
    // After the unlock: pushes the slot's rhythm and strip into the parameters, rebuilds a new or
    // reset rhythm's pattern and engine parameters, and releases what the edit parked.
    void syncSlotParams(int slot) override;
    // After an add: the default rhythm preset, as the editor's Add did.
    void layerAdded(int index) override { applyDefaultRhythm(index); }

    std::unique_ptr<Layer> makeRhythm(int slot) const;
    // A prepared voice engine for live slot `slot` (message thread, outside every lock).
    std::unique_ptr<VoiceEngine> makeSlotEngine(int slot) const;
    void resetPlayState(int slot);

    // A removed rhythm takes its voice engine with it: the base destroys the removed layer after the unlock, and
    // the bin cuts the removed slot's tails and frees them later, so nothing is freed under layersLock.
    // Edit bookkeeping between the locked hooks and syncSlotParams: the slot whose rhythm is new
    // (add / reset) and so needs its pattern and engine parameters rebuilt, and, during a remove or
    // swap, the last slot syncSlotParams will be given.
    int rebuildSlot     = -1;
    // The rhythms the parameter code treats as live: the layer count, except inside an edit (layersReordered sees
    // the count before a remove) and the legacy host restore, which opens all eight slots while it replaces the
    // parameter state so the listener applies every slot. Written on the message thread; read by the UI.
    std::atomic<int> liveRhythms { 0 };
    int reorderLastSlot = -1;
    // ── Preset orchestration internals (PluginProcessor_Preset.cpp / _HostState.cpp, message thread) ───────
    // Commit a pre-built full preset under suspend + layersLock, then finalise APVTS / mixer / globals: the
    // stopped load, the host restore and the stager's boundary commit. `prepared` is consumed.
    void commitStagedFullPreset(HotSwapStager::PreparedFullPreset& prepared, int originEff = 0,
                                uint32_t frameEpoch = RhythmCursor::kAnyFrameEpoch);
    // The legacy host-state format (the APVTS dump + r{i}_ properties), restored at once.
    void restoreStateFromTree(const juce::ValueTree& state);
    void restoreHostState(const void* data, int sizeInBytes);
    void restoreSession(const juce::ValueTree& root);   // a .muClid session tree + its extra parameter rows
    juce::ValueTree readRhythmPresetFile(const juce::File& file) const;
    PreparedRhythm  prepareRhythmPresetTree(const juce::ValueTree& state, int rhythmIndex, bool keepIdentity,
                                            const juce::String& source);
    bool            applyRhythmPresetTree(const juce::ValueTree& state, int targetIndex, bool keepIdentity,
                                          const juce::String& source);
    // The .muRhythm tree: a file (forFile) adds its metadata, name and colour; a clip does not.
    juce::ValueTree buildRhythmPresetTree(int rhythmIndex, bool embedSample, bool forFile,
                                          const juce::String& presetName, const juce::String& category,
                                          const juce::String& description);
    HotSwapStager::PreparedFullPreset prepareFullPreset(const juce::ValueTree& root) const;
    // The .muClid tree (shared by preset files and the host session) and its atomic write.
    juce::ValueTree buildFullPresetTree(const juce::String& name, const juce::String& description,
                                        const juce::String& category, bool embedSamples, bool forSession);
    bool            writeFullPresetFile(const juce::File& file, const juce::String& name,
                                        const juce::String& description, const juce::String& category,
                                        bool embedSamples);
    // The context a rhythm tree is read in: the Samples folder, the prepared rate and block size, onLoadError.
    RhythmPresetContext presetContext() const;
    // The legacy host-state sample restore for slot `slot` (the three property names to read).
    void restoreRhythmSample(int slot, const juce::ValueTree& tree, const juce::String& samplePathProp,
                             const juce::String& sampleDataProp, const juce::String& sampleNameProp);
    // The file name of the .muRhythm being staged, so load messages can name it; empty for a clip.
    juce::String loadSource;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    HotSwapStager  hotSwapStager { *this };

    friend class HotSwapStager;
    friend struct ClipCommitProbe;         // tests drive the boundary scan and commit of a staged launch

    // atomic for safe cross-thread access (audio writes, UI reads + clears).
    // Written in prepareToPlay; read in processBlock. JUCE calls prepareToPlay
    // while the audio thread is suspended by the host, so these fields are never
    // concurrently read and written — no atomic needed.
    double currentSampleRate = 44100.0;
    int    currentBlockSize  = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace mu_clid
