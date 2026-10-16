#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// CLAP sidechain support. The family's only audio input is a sidechain, not a main input. In
// VST3 this is handled by SidechainVST3Extensions::getPluginHasMainInput()=false (below); CLAP
// needs the parallel clap_juce_audio_processor_capabilities::isInputMain() override, or the CLAP
// wrapper flags input port 0 as CLAP_AUDIO_PORT_IS_MAIN and DAWs (e.g. Bitwig) won't show it as a
// sidechain. The header is only on the include path when this TU is compiled into a `_CLAP`
// target (clap_juce_extensions is PUBLIC there), so guard with __has_include — the VST3/Standalone
// builds compile with no CLAP dependency at all.
#if __has_include(<clap-juce-extensions/clap-juce-extensions.h>)
 #include <clap-juce-extensions/clap-juce-extensions.h>
 #define MU_CORE_HAS_CLAP 1
#else
 #define MU_CORE_HAS_CLAP 0
#endif

#include "Audio/FX/Slots/FXChain.h"
#include "Audio/MixerEngine.h"
#include "Audio/VoiceEngine.h"
#include "Persistence/MidiPresetMap.h"
#include "Persistence/MidiFullPresetMap.h"
#include "Control/MidiControlRouter.h"   // MIDI CC / note -> actions (map, learn, audio hand-off)
#include "Control/LiveState.h"          // Performance mode: the live state this instance owns
#include "Control/ParamGestureHold.h"
#include "Persistence/LayerState.h"   // composed slot / full / host state (format 2)
#include "MuLimits.h"
#include "Plugin/HotSwap.h"   // the shared loop-boundary swapper every layer preset hot-swap uses
#include "Sequencer/Layer.h"
#include "Plugin/ProcessorCore.h"   // layer accessors + published host transport
#include "Plugin/LayerBlock.h"      // the audio thread's one way to the layers (render contract)
#include "Plugin/TransportResolver.h"
#include "Sequencer/RenderContext.h"
#include "Plugin/RetireBin.h"
#include "Plugin/MidiClockSync.h"
#include "Plugin/ShellBackend.h"   // the interface the shared shell chrome consumes
#include "License/MachineFingerprint.h"
#include "License/OnlineActivation.h"   // OnlineActivationOutcome (decls only; .cpp is per-licensed-product)
#include "License/SupportPromptPolicy.h"   // isSupporter / when the support prompt shows

#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>

// Abstract base for all mu-family plugin processors.
//
// Owns the three pieces every mu-family plugin shares:
//   1. apvts  — the APVTS instance. Derived plugins supply the layout
//               at construction (createParameterLayout()).
//   2. fxChain + mixerEngine — shared DSP plumbing.
// Provides processCoreBlock() as the common per-block mixing entry point.
// Derived classes supply the trigger engine (Euclidean, MIDI, etc.) and
// call processCoreBlock() from their own processBlock().
//
// Keeping apvts on the base lets mu-core UI components (MixerChannel, FXRow,
// MixerOverlay) take a `ProcessorBase*` instead of forward-declaring each
// plugin's concrete `PluginProcessor` type — eliminates the layering
// violation that previously had mu-core including mu-clid headers.
class ProcessorBase : public ProcessorCore,
                      public mu_core::ShellBackend,   // what the shared shell chrome reads from the app
                      public mu_core::ControlSink     // performs the family control actions (MIDI map, surfaces)
#if MU_CORE_HAS_CLAP
                    , public clap_juce_extensions::clap_juce_audio_processor_capabilities
#endif
{
public:
    ProcessorBase(const BusesProperties& props,
                  juce::AudioProcessorValueTreeState::ParameterLayout layout,
                  const juce::Identifier& stateTreeType = juce::Identifier("MuFamilyState"));
    ~ProcessorBase() override;

    // Public so UI panels (MixerOverlay, FXRow, MixerChannel, etc.) can access
    // them directly — matches the layout PluginProcessor had before extraction.
    // `apvts` must be the FIRST data member: ProcessorBase's other members and
    // derived-class members may depend on it during their construction.
    juce::AudioProcessorValueTreeState apvts;
    FXChain     fxChain;
    MixerEngine mixerEngine;

    // Channel metadata for the shared mixer UI (getNumLayers, getLayer, getLayerName, ...)
    // is declared in ProcessorCore.

    // The gain-reduction meter of layer `i`'s insert effect (null when it has none or `i` is out of
    // range), for the shared insert panel's compressor / limiter meter. Message thread only.
    const std::atomic<float>* getInsertGR(int i) const
    {
        const auto* layer = getLayer(i);
        return layer != nullptr ? layer->insertGainReduction() : nullptr;
    }

    // ─── MIDI program-change → preset loading ────────────────────────────────
    // The maps + FIFO + audio-thread scan + message-thread drain live here so
    // every mu-family plugin gets the same MIDI PC behaviour without
    // duplication. The plugin-specific bits are exposed as virtuals:
    //   - The per-slot preset directory + file extension (for the editor UI).
    //   - The full-preset directory + file extension (for the editor UI).
    //   - applyMidiPresetLayer / applyFullMidiPreset — how this plugin actually
    //     loads the file (mu-clid stages a rhythm preset / defers a full
    //     preset to the loop point; mu-tant will define its own semantics).
    // Public so the shared MIDI editor panels can read/write directly.
    MidiPresetMap     midiPresetMap;
    MidiFullPresetMap midiFullPresetMap;

    virtual juce::File   getLayerPresetDir()       const = 0;
    virtual juce::String getLayerPresetExtension() const = 0;
    juce::File           getFullPresetDir()          const override { return getPresetsDir(); }
    juce::String getFullPresetExtension()    const override = 0;

    // ─── Structural layer edits (message thread only) ─────────────────────────
    // The one routine every product's add / delete / reorder / reset goes through, in the order
    // the layer-ownership contract fixes (design-plugin-family.md, "Layer ownership and structural
    // edits"): cancel the affected staged swaps; suspend processing and raise layerEditInFlight (suspend is no barrier in every host; layersLock protects the moves);
    // build the new layer before locking; under layersLock move the layer pointers and the mixer
    // strip state and re-point the sidechain sources; unlock, then move the parameter values with
    // processing still suspended; resume; destroy any removed layer last, outside every lock.
    // They need createLayer(); a product with a fixed count does not supply it and these refuse.
    //   addLayer     appends a default layer; returns its index, or -1 when full or unsupported
    //   removeLayer  deletes a layer, the ones above shift down; the last layer cannot be removed
    //   swapLayers   exchanges two layers (reorder). Virtual, like resetLayer, for the same reason.
    //   resetLayer   puts one layer back to defaults, keeping its name and colour. Virtual because
    //                the products that have not moved their layers into the container reset their own.
    // True when the product has moved its layers into the container and supplies createLayer, so
    // the three edits above work. Products with a fixed count leave it false.
    virtual bool canEditLayers() const { return false; }
    // True when the layer sidebar may offer drag-to-reorder (swapLayers works). Normally the same
    // as canEditLayers; a product that reorders its layers some other way overrides both.
    virtual bool canReorderLayers() const { return canEditLayers(); }
    int          addLayer();
    bool         removeLayer(int idx);
    virtual bool swapLayers(int a, int b);
    virtual void resetLayer(int layer);

    // Called by the four edits above, on the message thread with layersLock held and processing
    // suspended, once the layers, strip state and onMoved calls are done: the slots firstIndex..lastIndex
    // (inclusive) now hold a different layer than before (added, shifted down, exchanged or reset;
    // a swap calls it once per exchanged slot). A product resets the audio state it keeps per slot
    // (filter and effect memory, tails) here. The audio thread cannot be rendering a slot meanwhile
    // (a product that edits layers try-locks layersLock per block), so it must only reset, not
    // allocate or wait.
    virtual void layersMoved(int firstIndex, int lastIndex) { juce::ignoreUnused(firstIndex, lastIndex); }

    // Which structural edit is reordering layers, for layersReordered.
    enum class LayerEdit { Remove, Swap };

    // Called by removeLayer and swapLayers, immediately before layersMoved, under layersLock with
    // processing suspended, after the layer pointers, strip state and onMoved calls are done. It tells
    // a product what layersMoved(first, last) cannot: where the data at a slot came from. Remove: `a`
    // is the index removed (every slot above it moved down one, and `b` is -1). Swap: slots `a` and
    // `b` exchanged. A product that keeps per-slot state of its own (engines, tails, play state)
    // moves it here; like layersMoved it must only move and reset, never allocate or wait.
    virtual void layersReordered(LayerEdit kind, int a, int b) { juce::ignoreUnused(kind, a, b); }

    // True when the product's own layer data, not the parameter tree, is the truth for the slot's
    // sound (mu-Clid's Rhythm, where the parameters mirror it). The edits then do not copy parameter
    // values between slots in the tree: they call syncSlotParams for every slot the edit touched
    // (add: the new slot; remove: the removed index and every slot above it, the vacated top one
    // included; swap: both slots; reset: that slot), and the product pushes that slot's layer data,
    // and its mixer strip state, into the parameters.
    virtual bool layerHoldsParamTruth() const { return false; }

    // Message thread, not under layersLock, after the pointer moves. Only called when
    // layerHoldsParamTruth() is true, and then processing has already resumed and layerEditInFlight is
    // down (the audio never reads these parameters, so the push must not silence the mix); a slot
    // that is live meanwhile must therefore already be safe to render. A product also frees here
    // whatever it parked under the lock.
    virtual void syncSlotParams(int slot) { juce::ignoreUnused(slot); }

    // Called by addLayer last, on the message thread, not under layersLock, with the new layer's
    // parameters at their defaults, so a product can give the new layer its own starting point (a
    // product with per-layer engine types picks the engine). Processing is still suspended unless
    // layerHoldsParamTruth() is true, where it has resumed: that product may do file work here.
    virtual void layerAdded(int index) { juce::ignoreUnused(index); }

    // ─── Shell-facing API (overridden by each plugin) ────────────────────────
    // These are the surfaces the family's editor shell + TransportBar consume.
    // Defaults are no-ops returning sensible values so a young plugin (no
    // internal transport yet, no presets, no license gate) still builds and
    // shows the shell. mu-clid overrides every one of these against its real
    // implementations; mu-tant currently inherits the defaults for everything
    // except whatever it has implemented.

    // Internal transport (TransportBar play / BPM controls): a free-running clock each product
    // advances in processBlock while no host / MIDI clock drives it. Stopping resets the beat.
    bool   isInternalPlaying()      const noexcept override { return internalPlaying.load(std::memory_order_relaxed); }
    void   toggleInternalPlay() override
    {
        const bool now = ! internalPlaying.load(std::memory_order_relaxed);
        internalPlaying.store(now, std::memory_order_relaxed);
        if (! now) internalBeatPos.store(0.0, std::memory_order_relaxed);   // restart patterns cleanly
    }
    double getInternalBpm()         const noexcept override { return internalBpm.load(std::memory_order_relaxed); }
    void   setInternalBpm(double bpm) override     { internalBpm.store(juce::jlimit(20.0, 300.0, bpm), std::memory_order_relaxed); }
    double getInternalBeatPos()     const noexcept override { return internalBeatPos.load(std::memory_order_relaxed); }

    // The standalone wrapper owns the transport; a plugin mirrors the host's.
    bool isStandaloneApp() const noexcept override { return wrapperType == wrapperType_Standalone; }

    // The host transport readers live in ProcessorCore (non-virtual there); these forward to them so
    // the shell chrome reads them through the ShellBackend interface.
    bool isHostPlaying() const noexcept override { return ProcessorCore::isHostPlaying(); }
    void getHostTimeSignature(int& numerator, int& denominator) const noexcept override { ProcessorCore::getHostTimeSignature(numerator, denominator); }
    bool getHostBarStartPpq(double& ppq) const noexcept override { return ProcessorCore::getHostBarStartPpq(ppq); }
    bool getHostPpqPosition(double& ppq) const noexcept override { return ProcessorCore::getHostPpqPosition(ppq); }

    // The family bus layout: at most one sidechain input (stereo or disabled) and a stereo
    // main output. Products with extra output buses (mu-Clid's multi-out) override it.
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    // Master loop — the global loop the shared mu-core MasterLoopSection displays
    // (and which products may use to gate preset/program-change swap timing). The
    // length lives in the `mstrLoop` APVTS param (0 = free; 1..16 → 16..256 steps,
    // 16 steps = 1 bar); products turn that into a step total + a live counter.
    // Default 0/off so a product without a master loop simply shows nothing.
    virtual int    getMasterLoopSteps()       const        { return 0; }
    virtual int    getMasterLoopCurrentStep() const        { return 0; }

    // MIDI clock sync (standalone) — shared by every product; the enable / messages choices
    // are saved in the app settings file.
    bool   getMidiSyncEnabled()  const noexcept override { return midiClockSync.isEnabled(); }
    int    getMidiSyncMessages() const noexcept override { return midiClockSync.getMessages(); }
    double getMidiClockBpm()     const noexcept override { return midiClockSync.getBpm(); }
    bool   isMidiClockPlaying()  const noexcept override { return midiClockSync.isPlaying(); }
    MidiClockSync::ClockState getMidiClockState() const noexcept override { return midiClockSync.getClockState(); }
    void   setMidiSyncEnabled(bool on) override;
    void   setMidiSyncMessages(int mode) override;

    // Sync offset (standalone, saved in the app settings): how many ms the app plays EARLIER than the
    // external clock says, to cancel the audio output latency. Applied to the resolved beat under MIDI clock.
    // The range (kSyncOffsetMinMs / MaxMs) is declared on ShellBackend.
    int    getSyncOffsetMs() const noexcept override { return syncOffsetMs.load(std::memory_order_relaxed); }
    void   setSyncOffsetMs(int ms) override;

    // The standalone transport's meter (saved): changes the displayed bar.beat.sixteenth only, patterns
    // stay four beats a bar. A plugin reads the host's meter instead.
    void   getTimeSignature(int& numerator, int& denominator) const noexcept override
    {
        const int packed = timeSignature.load(std::memory_order_relaxed);
        numerator = packed >> 8;  denominator = packed & 0xFF;
    }
    void   setTimeSignature(int numerator, int denominator) override;

    // Nudge buttons (standalone): -1 slower, +1 faster while held, 0 released (both held = 0).
    void   setNudgeDirection(int direction) override { nudgeDirection.store(juce::jlimit(-1, 1, direction), std::memory_order_relaxed); }
    // Sets MIDI clock sync for this process only, without saving it (the headless render uses it so
    // a render never depends on the user's saved choice).
    void   setMidiSyncForSession(bool on, int mode) { midiClockSync.setMessages(mode); midiClockSync.setEnabled(on); }

    // Presets — directory, save/load, and the shared category list. A product that names its
    // full-preset root tag (getFullPresetTag) gets the standard save / load / category list:
    // save writes captureFullPreset() wrapped with name / description / category; load reads it
    // (reporting errors through onLoadError), hands the tree to useLoadedFullPreset (stage it while
    // playing, apply it while stopped) and publishes the name. With no tag these do nothing and
    // the product overrides them (mu-Clid's PresetIO).
    // Full presets live in <content dir>/Presets in every product.
    juce::File                 getPresetsDir()                                     const override;
    void                       loadPreset(const juce::File& file) override;
    void                       savePreset(const juce::String& name,
                                           const juce::String& desc,
                                           const juce::String& cat,
                                           bool embedSamples) override;
    juce::StringArray          loadCategoryList()                                  const override;
    virtual const char*        getFullPresetTag()                                  const { return nullptr; }
    virtual juce::ValueTree    captureFullPreset()                                       { return apvts.copyState(); }
    virtual void               useLoadedFullPreset(juce::ValueTree /*state*/)            {}
    void                       ensureCategoryInList(const juce::String& /*cat*/)         override {}
    bool                       hasPendingFullPreset()                              const override { return layerSwapper.hasFullPending(); }
    // Write the current state as a full preset at exactly `file` (headless render --save-preset).
    // Default: the standard wrapped preset; false (after onLoadError) when it can't be written.
    virtual bool               saveFullPresetTo(const juce::File& file);

    // Headless render: the render loop never returns to the message loop, so neither the swap poll
    // swap poll timer is serviced. Call after each processBlock so deferred work (hot-swap commits, MIDI
    // program changes, control actions) runs on the calling thread. No-op when nothing is pending.
    void flushPendingAsyncUpdates()
    {
        drainDeferredWork();
        consumeMuteChange();
        if (retireBin.hasPendingWork()) retireBin.service();   // a tail that went Done is freed off the render loop too
    }

    // ─── Test accessors for the layer extraction (approved, E0.9) ─────────────────────────────────
    // The `useLayerRender` switch: while a stage moves a product's render chain, the old chain and
    // Layer::render coexist behind it (extraction-plan 2.7a); a product reads usesLayerRender() in
    // processBlock and the equivalence test (mu-core/Tests/LayerEquivalence.h) flips it on one of two
    // processors. Default off; never set by production code. A switch that outlives its stage is a defect.
    void setUseLayerRenderForTest(bool on) noexcept { useLayerRender.store(on, std::memory_order_relaxed); }
    bool usesLayerRender() const noexcept           { return useLayerRender.load(std::memory_order_relaxed); }
    // The per-channel tap: what each channel hands the mixer (MixerEngine::ChannelTap). Set while not processing.
    void setChannelTapForTest(MixerEngine::ChannelTap* tap) noexcept { mixerEngine.channelTap = tap; }

    // What the shared block driver did, for tests (audio-thread values, read while not processing).
    struct LayerRenderProbe
    {
        int  mixCount = 0;          // channels handed to the mixer by the last rendered block
        int  blocksRendered = 0;    // host blocks renderAndMix rendered
        int  blocksSkipped = 0;     // host blocks it did not (a missed lock, an edit in flight, 0 frames)
        int  largestCall = 0;       // the longest call any layer or the mixer was given (chunking bound)
        int  midiDropped = 0;       // input events the last block's decoder had no room for
        bool jumpPending = false;
        bool lastPlaying = false;
    };
    // The live count as the packed layout word holds it, and as the numLayers mirror holds it: equal after every
    // count store (ProcessorCore::storeLayerCount). Tests only.
    int layoutWordCountForTest() const noexcept { return (int) mu_core::LayoutWord::count(layerAccess.layout.load()); }
    int layerCountMirrorForTest() const noexcept { return numLayers.load(); }
    // Tests only, message thread, not processing, after the product's prepareToPlay: renders one playing block of
    // n frames through renderAndMix on a scratch buffer and returns what the driver did (prepares and releases the
    // driver around it). A product whose layers have not adopted Layer::render renders silence, so the check is
    // that the driver saw every live layer (mixCount), not the sound.
    LayerRenderProbe renderOneLayerBlockForTest(double sampleRate, int n);

    LayerRenderProbe getLayerRenderProbe() const noexcept
    {
        return { renderState.mixCount, renderState.blocksRendered, renderState.blocksSkipped, renderState.largestCall, (int) renderState.dropped,
                 renderState.jumpPending, renderState.lastPlaying };
    }

    // Default preset: <presets>/_default.<ext>, written by the shell's "Save as Default". Each
    // product calls loadStartupDefault() at the end of its constructor so a saved default is
    // restored on launch (a host session restore then replaces it). Render mode sets
    // skipAutoLoadDefault first so headless renders start from the factory state.
    juce::File         getDefaultPresetFile() const override;
    virtual void       loadDefaultPreset();
    void               loadStartupDefault() { if (! skipAutoLoadDefault) loadDefaultPreset(); }
    inline static bool skipAutoLoadDefault = false;

    // Per-slot (layer) presets — one rhythm / voice / layer / lane. The shared header bar and the
    // MIDI program-change slots drive these; the product implements save / load / reset. A load
    // while playing is staged for the slot's boundary (hasPendingSwap until it lands).
    // onLayerPresetCommitted fires on the message thread once a slot preset has been applied, so
    // the editor can refresh what APVTS attachments don't cover. The editor must clear it in its
    // destructor (the processor can outlive the editor).
    virtual juce::Array<juce::File> slotPresetFiles(int slot) const;   // default: every file in the per-slot dir
    virtual void saveLayerPreset(int /*slot*/, const juce::String& /*name*/) {}
    virtual void loadLayerPreset(int /*slot*/, const juce::File& /*file*/)   {}

    // The layer's state as the tree its layer-preset file holds (root tag included), without the file.
    // A clip is exactly this tree. Default: an invalid tree (the product has no clips).
    virtual juce::ValueTree captureLayerPresetTree(int /*slot*/)             { return {}; }

    // Clip bank (message thread only). A clip is a layer-preset tree kept in the layer's ClipBank;
    // launching one is a layer-preset load from that tree, so a stopped transport applies it now and a
    // playing one commits it at the layer's swap boundary. The bank is saved with full presets and
    // sessions only (see captureComposedState).
    // An empty `name` is replaced by clipLabel(slot): a clip is named after the preset it was stored from.
    bool    storeClip(int slot, int clip, const juce::String& name = {});
    // The name a clip stored from `slot` gets: the preset the layer last came from, else the layer's own name.
    // A product with a richer notion of "the preset" overrides it.
    virtual juce::String clipLabel(int slot) const;
    // A product calls this once a layer-preset file is loaded into, or saved from, `slot` (stem, no extension).
    void    noteLayerPresetLabel(int slot, const juce::String& label);
    bool    clearClip(int slot, int clip);
    // Reads a layer-preset file into a clip. Refuses (onLoadError, bank unchanged) a file that cannot
    // be read or whose root tag is not this product's layer-preset tag.
    bool    loadClipFromFile(int slot, int clip, const juce::File& layerPreset);
    // False (nothing changed) when the layer or clip is out of range, the clip is empty, a full
    // preset is staged, or the stage hook staged nothing (a clip that cannot be built). The hook must change
    // slotStageSerial(slot) whenever it stages or applies a payload, as every stage path does.
    bool    launchClip(int slot, int clip);
    uint8_t clipMask(int slot) const;
    // How many clips layer `slot` stores (0 for no such layer).
    int     clipCount(int slot) const
    {
        const unsigned mask = clipMask(slot);
        int n = 0;
        for (int i = 0; i < mu_limits::kClipsPerLayer; ++i)
            n += (int) ((mask >> i) & 1u);
        return n;
    }

    virtual bool hasPendingSwap(int slot) const                             { return layerSwapper.hasPending(slot); }
    // Drops the staged (not yet committed) full-preset swap, if any (Performance mode cancelling a queued item).
    virtual void cancelPendingFullPreset()                                  { layerSwapper.cancelFull(); }
    // Changes whenever any full preset is staged (Performance mode tells its own staging from another load).
    virtual uint32_t fullStageSerial() const                                { return layerSwapper.fullSerial(); }
    // Changes whenever a full preset is actually applied (committed at its boundary or applied at once), never
    // when one is only staged or cancelled: the live state drops its clip state on this, not on a stage.
    uint32_t fullCommitCount() const noexcept                               { return layerSwapper.fullCommitCount() + productFullCommits; }
    // For a product that applies full presets itself, not through layerSwapper (mu-Clid): call it at
    // each apply, message thread.
    void noteFullPresetApplied() noexcept                                   { ++productFullCommits; }
    // Changes whenever layer `slot` is staged or cancelled, never when its swap commits (the live state tells a
    // clip launch from a later layer-preset load on the same layer).
    virtual uint32_t slotStageSerial(int slot) const                        { return layerSwapper.slotSerial(slot); }

    // Performance mode's live state (message thread only): read through the view, changed through perform().
    mu_core::LiveState&       getLiveState() noexcept       { return liveState; }
    // Host-session persistence: the products call these on the root they already save and load (the session
    // only: never a preset file, never the APVTS). Restore AFTER the session's preset is applied.
    void saveLiveState(juce::ValueTree& root) const         { liveState.saveTo(root); }
    // A full-preset load that could not read its file calls this (and reports through onLoadError as before):
    // Performance mode tells that apart from a load that succeeded with warnings.
    void noteFullPresetLoadFailed() noexcept                { fullPresetLoadFailed.store(true, std::memory_order_relaxed); }
    void restoreLiveState(const juce::ValueTree& root)      { liveState.restoreFrom(root); }
    // The ShellBackend's Performance mode accessors: the shell reads the live state and performs actions through this instance.
    const mu_core::LiveView*  getLiveView() const override  { return &liveState; }
    mu_core::ControlSink*     getControlSink() override     { return this; }
    bool                      supportsPerformanceMode() const override { return isStandaloneApp(); }
    // The standalone shell's controller handle (non-owning; the shell clears it before it destroys the attachment).
    mu_core::ControlAttachment* getControlAttachment() const override { return controlAttachment; }
    void                      setControlAttachment(mu_core::ControlAttachment* a) noexcept { controlAttachment = a; }
    juce::String              getProductStateId() const override { return apvts.state.getType().toString(); }
    bool                      loadPerformance(mu_pp::PerformanceFile file, const juce::File& where) override
    { return liveState.loadPerformance(std::move(file), where); }
    // Drops the staged (not yet committed) per-layer swap for `slot`, if any. A structural edit
    // calls it for every layer it renumbers or clears, because a staged swap is keyed by index and
    // would otherwise land on the wrong layer. A staged full preset is left to win.
    virtual void cancelPendingSwap(int slot)                                { layerSwapper.cancel(slot); }
    std::function<void(int slot)> onLayerPresetCommitted;

    // Set by the standalone (mu-link bridge) so the product can publish its current full-preset
    // name for display on the mu-link mixer. Null in plugin builds / when mu-link isn't wired.
    // Products call publishPresetName(...) from their loadPreset override.
    std::function<void(const juce::String&)> onPresetNameChanged;
    void publishPresetName(const juce::String& name) { if (onPresetNameChanged) onPresetNameChanged(name); }

    // Content directory — where preset / sample-library / keybindings folders live:
    // Documents/TDP/<appName> (the name given to initAppSettings), or a folder the user
    // picked (saved as "contentDir"). Invalid before initAppSettings; the shell tolerates that.
    juce::File getContentDir() const override;

    // Tests only (message thread): while one is alive, initAppSettings puts the settings file and the
    // *_midiControl.json map under <root>/settings/ and getContentDir() defaults to
    // <root>/content/<appName>, so building a processor never touches the user's real TDP folders.
    // Production code never creates one. Nestable: the destructor restores the previous root.
    class ScopedStorageRoot
    {
    public:
        explicit ScopedStorageRoot(const juce::File& root);
        ~ScopedStorageRoot();
        ScopedStorageRoot(const ScopedStorageRoot&) = delete;
        ScopedStorageRoot& operator=(const ScopedStorageRoot&) = delete;
    private:
        juce::File previous;
    };

    // Licence state. A product with no licensing model (it never calls initLicensing) always counts as
    // licensed; otherwise licensed means the offline signed .lic verifies OR the machine is
    // online-activated. Nothing is gated on it: it only marks a supporter (see isSupporter).
    bool hasLicensing() const noexcept { return licensingEnabled; }   // initLicensing was called
    virtual bool isLicensed() const
    {
        return ! licensingEnabled || offlineLicensed || isOnlineActivated();
    }

    // There are no feature limits: an unlicensed copy is fully functional (it only shows the
    // support prompt), so nothing here gates channels, steps or saving.

    // ─── Supporter state + the support prompt ────────────────────────────────
    // A supporter holds a valid key (offline .lic or online activation). Unlike isLicensed() this
    // is the same in Debug and Release, and it gates only the polite support prompt and About.
    bool isSupporter() const noexcept
    {
        return mu_core::support_prompt::isSupporter(licensingEnabled, offlineLicensed, isOnlineActivated());
    }
    // Where the prompt's "Pay what you like" button opens (empty when the product supplies none).
    juce::String getCheckoutUrl() const { return checkoutUrl; }
    // "Don't show this again": kept in the app settings file, not APVTS, so it is never in a preset.
    bool isSupportPromptHidden() const { return mu_core::support_prompt::isHidden(appSettings.get()); }
    void setSupportPromptHidden(bool hidden) { mu_core::support_prompt::setHidden(appSettings.get(), hidden); }

    // ─── Online activation (Lemon Squeezy, Phase 1) ──────────────────────────
    // Licensed products set activateOnlineFn (a lambda calling mu_core::OnlineActivation,
    // compiled into the product) and flip onlineActivated at startup + on success. The
    // shell's activation overlay invokes activateOnlineFn OFF the message thread; unlicensed
    // products leave it null, so the overlay simply reports "unavailable". isLicensed() in a
    // licensed product returns true when the offline `.lic` OR the online activation is valid.
    std::function<mu_core::OnlineActivationOutcome(const juce::String& licenseKey)> activateOnlineFn;
    std::atomic<bool> onlineActivated { false };
    bool isOnlineActivated() const noexcept { return onlineActivated.load(std::memory_order_relaxed); }

    // The machine "challenge" code shown in the overlay's offline path (and what a `.lic` is
    // bound to). juce_core-only, safe to call from any product/the shell.
    juce::String licenseChallengeCode() const { return mu_core::MachineFingerprint::getShortCode(); }

    // ─── UI scale (Medium baseline, Large 1.25x) ─────────────────────────────
    // Storage lives on the base so every plugin inherits the same scale handling; the choice
    // is saved in the app settings file (saved before the change is announced, so a listener
    // that re-reads the file sees the new value).
    // kUiScaleMedium / kUiScaleLarge are declared on ShellBackend.
    float getUiScale() const noexcept override { return uiScale; }
    void  setUiScale(float scale) override;

    // Master level (the settings page's Audio row): the mstr_lvl parameter, written through the
    // parameter so the host sees the change.
    bool  hasMasterLevel() const override { return apvts.getParameter("mstr_lvl") != nullptr; }
    float getMasterLevel() const override
    {
        if (auto* raw = apvts.getRawParameterValue("mstr_lvl")) return raw->load();
        return 1.0f;
    }
    void  setMasterLevel(float level) override
    {
        if (auto* param = apvts.getParameter("mstr_lvl"))
            param->setValueNotifyingHost(param->convertTo0to1(level));
    }

    // Call at the end of a host state restore (setStateInformation, message thread): the restore changed
    // parameter values behind the host's back, so tell it to re-read them. The CLAP wrapper turns this
    // into a values rescan, which the CLAP spec requires after a state load.
    void notifyHostStateRestored()
    {
        updateHostDisplay(ChangeDetails().withProgramChanged(true));
    }

    // The shell callbacks (onSaveAndQuit, onLoadError, onPresetSwapCommitted, onUiScaleChanged) are
    // members of ShellBackend. The editor MUST clear them in its dtor: the processor can outlive
    // the editor when a DAW keeps the plugin loaded after closing the window.

    // Audio-thread: call once per processBlock, before the engine reads the MIDI. Queues the
    // block's program changes (scanMidiProgramChanges) and the CC / notes the control map claims
    // (those are taken out of `midi`), and asks the message thread (mailbox flag, drained by the swap
    // poll) to load / perform them via applyMidiPresetLayer / applyFullMidiPreset / perform.
    void queueMidiProgramChanges(juce::MidiBuffer& midi)
    {
        const bool presets = scanMidiProgramChanges(midi);
        const bool control = midiControlRouter.process(midi);
        if (presets || control)
            requestDeferredWork();
    }

    // Audio-thread: call right after the block's transport is resolved, so the next block's
    // quantised control actions (mute / solo / stop on the beat or bar) know where the beat is.
    void noteBlockTransport(const mu_core::BlockTransport& t) noexcept
    {
        int num = 4, den = 4;
        getHostTimeSignature(num, den);
        midiControlRouter.noteBlock(t, num > 0 && den > 0 ? (double) num * 4.0 / (double) den : 4.0);
    }

    // The MIDI control map (CC / note -> action) and its performer. UI and settings reach it here.
    mu_core::MidiControlMap& getMidiControlMap() noexcept { return midiControlMap; }
    bool perform(const mu_core::ControlAction& action) override;

    // Audio-thread: scans incoming MIDI for program-change messages on
    // channels 1-8 (per-slot map, gated by `midiPresetMap.getChannelMask()`)
    // and channel 9 (full-preset map, gated by `midiFullPresetMap.isEnabled()`).
    // Each matching PC is enqueued into the lock-free FIFO. Returns true if
    // any PCs were enqueued. Products call queueMidiProgramChanges instead.
    bool scanMidiProgramChanges(const juce::MidiBuffer& midi);

    // Message-thread: drains the FIFO and dispatches each event via the
    // applyMidiPresetLayer / applyFullMidiPreset virtuals. Run by runDeferredWork.
    void drainPendingMidiProgramChanges();

    // Maps a shared global-FX / return / master / channel-strip APVTS parameter
    // to fxChain + mixerEngine state; the base's FX listener (startFxParamSync) routes
    // every matching ID here (the param set is declared by mu_mixfx::addGlobalFxParams
    // + the product's `ch{i}_*` strip). Handles: `ch{i}_*`, `ret_*`, `mstr_lvl/pan`,
    // `mst_ins*`, `eff_*`, `eff2*`, `dly_*`, `rev_*`, `echo_*`. Unrecognised IDs no-op.
    void syncGlobalFxParam(const juce::String& id, float v);

protected:
    // Composed state (Persistence/LayerState.h, format 2) — the slot unit every product saves and
    // loads. A product describes its slots once in its constructor (each slot's param prefix + its
    // non-parameter data, which each Layer saves and loads through its writeExtras / applyExtras), then layer presets, full presets and host sessions all go through these,
    // so a slot is written and applied by the same code wherever it appears.
    // The layer behind persistence slot `i`: every declared slot, active or not, so a state with
    // more layers than are live still loads (and saves) their data. A product that keeps every slot
    // filled (keepSpareLayers) reaches the spares too; any other reaches the live layers.
    Layer* slotLayer(int i)
    {
        if (keepSpareLayers) return (i >= 0 && i < mu_limits::kMaxLayers) ? layers[(size_t) i].get() : nullptr;
        return getLayer(i);
    }
    // `legacyPrefixes` (same slot order) are the prefixes a renamed product used before; only the
    // rebuild of an older APVTS-dump state reads them (toComposedState), never a live id.
    void initLayerState(const juce::StringArray& layerPrefixes, const juce::StringArray& legacyPrefixes = {})
    {
        layerLayout = mu_pp::LayerLayout(*this, layerPrefixes);
        layerLayout.addLegacyPrefixes(legacyPrefixes);
        layerExtras = { [this](int slot, juce::ValueTree& node)       { if (auto* l = slotLayer(slot)) l->writeExtras(node); },
                       [this](int slot, const juce::ValueTree& node) { if (auto* l = slotLayer(slot)) l->applyExtras(node); } };
    }
    // A product whose canEditLayers() is true also saves the live layer count (`numVoices`) and the
    // palette colour of each slot (`voiceColours`, 8 comma-separated indices) as root properties; the
    // names are legacy saved data and do not change. A layer preset never carries them.
    juce::ValueTree captureComposedState();
    // Applies the parameters and extras, then (canEditLayers only) stores the count last and restores
    // the colours, so the audio thread never sees a count ahead of the data.
    void            applyComposedState(const juce::ValueTree& state);
    // The live layer count a composed state carries, clamped to 1..kMaxLayers; `unsavedLayerCount`
    // when the state has none (an older file). A product that must act on the old and the new count
    // before the apply reads it here.
    int             savedLayerCount(const juce::ValueTree& state) const;
    // The count a state without `numVoices` loads with: the product's count before layers were
    // editable. Set once in the product's constructor.
    int unsavedLayerCount = 1;
    juce::ValueTree captureLayerNode(int slot, const juce::Identifier& type) { return mu_pp::captureLayer(layerLayout, layerExtras, slot, type); }
    void            applyLayerNode(int slot, const juce::ValueTree& node)    { mu_pp::applyLayer(layerLayout, layerExtras, slot, node); }
    // A state in the composed shape: an older APVTS-dump state is rebuilt (`otherChild` receives
    // the product's older root children); a composed one is returned as is.
    juce::ValueTree toComposedState(const juce::ValueTree& state,
                                    const std::function<void(const juce::ValueTree&, juce::ValueTree&)>& otherChild = {}) const
    {
        return mu_pp::composeLegacyState(state, layerLayout, otherChild);
    }
    mu_pp::LayerLayout layerLayout;
    mu_pp::LayerExtras layerExtras;

    // Re-points every sidechain source parameter (the channel strips' ch{N}_scSrc and the three return
    // strips) through `remap(paramValue)`. The parameter is 0 = off, 1..8 = channel 0..7, 9 = the
    // external bus; a structural edit renumbers the layers, so a strip must follow its source's new slot.
    void remapSidechainSources(const std::function<int(int)>& remap);

    // A parameter whose value names another layer (such as a processing layer's source layer) must follow its layer
    // through removeLayer and swapLayers, like the sidechain sources. Called in phase 3 of both, with
    // processing suspended and before the parameter values shift; `oldToNew` maps a layer index as it
    // was to the index it has now, or -1 when that layer is gone. The product rewrites its own
    // parameters through it. Default: nothing to rewrite. addLayer and resetLayer do not call it.
    virtual void remapLayerReferences(const std::function<int(int)>& oldToNew) { juce::ignoreUnused(oldToNew); }

protected:
    // Internal transport state. Written by the UI (play / BPM) and the audio thread (beat
    // advance); relaxed atomics — each is a lone published value.
    std::atomic<bool>   internalPlaying { false };
    std::atomic<double> internalBeatPos { 0.0 };
    std::atomic<double> internalBpm     { 120.0 };
    std::atomic<int>    syncOffsetMs    { 0 };
    std::atomic<int>    nudgeDirection  { 0 };     // UI → audio: -1 / 0 / +1 while a nudge button is held
    std::atomic<double> nudgePhaseBeats { 0.0 };   // audio thread: the nudge shift accumulated under MIDI clock
    std::atomic<double> appliedSyncOffsetBeats { 0.0 };   // audio thread: the sync offset applied so far (it ramps)
    std::atomic<int>    timeSignature   { (4 << 8) | 4 };   // numerator << 8 | denominator, one value so it can't tear

    // pollHostTransport() (publishes the host playhead for the UI) lives in ProcessorCore.

    // Message thread (or the headless render loop's own thread): run the deferred work now if the
    // audio thread asked for it. Returns true when it ran.
    bool drainDeferredWork()
    {
        mu_hotswap::assertMessageThread();
        return swapMailbox.drain([this] { runDeferredWork(); });
    }

    // Deferred message-thread work first (product hot-swap commits), then program changes and
    // control actions.
    void runDeferredWork()
    {
        retireBin.service();   // frees first, so an install that follows never finds a row of reusable Done entries full
        if (layerSwapper.commit())
        {
            liveState.fullPresetCommitted();   // before the editor refresh, so it reads the settled state
            if (onPresetSwapCommitted) onPresetSwapCommitted();
        }
        commitDeferredWork();
        settleLiveState();   // after the product's commits: a queued item or clip that landed or was superseded
        drainPendingMidiProgramChanges();
        // Left queued by a table edit in progress: ask for another pass so it is not stranded.
        if (midiControlRouter.drain(*this))
            requestDeferredWork();
    }

    // Message-thread work the audio thread asked for (hot-swap commits at a loop boundary, via
    // the swap poll; MIDI program changes and control actions, via the same mailbox). Runs before
    // queued program changes are loaded. Default: nothing.
    virtual void commitDeferredWork() {}

    // The staged preset swaps (a parsed state tree per layer plus one full preset), shared by every
    // product that does not need a richer payload. A product sets its appliers once in its
    // constructor and calls flagSwapBoundaries from processBlock; the commit runs in
    // runDeferredWork. A product with its own stager (mu-Clid) overrides the three swap queries.
    // Audio-thread flagging uses atomics only; the appliers run on the message thread.
    mu_hotswap::LayerSwapper<juce::ValueTree, mu_limits::kMaxLayers> layerSwapper;
    mu_core::ControlAttachment* controlAttachment = nullptr;   // message thread only
    mu_core::LiveState liveState;   // Performance mode (message thread only; never read on the audio thread)
    uint32_t productFullCommits = 0;                       // full presets applied outside layerSwapper (message thread only)
    // The tree of each layer's last non-clip stage that is still waiting for its boundary (invalid when none).
    // A store captures it instead of the live layer, which does not show that preset yet.
    std::array<juce::ValueTree, mu_limits::kMaxLayers> pendingPresetTree;
    // Per slot, the preset (file stem or clip name) the layer's state last came from; empty = none known.
    // Message thread only; follows its layer through remove / swap, and is forgotten by a non-clip stage and a reset.
    std::array<juce::String, mu_limits::kMaxLayers> presetLabels;
    int lastTransportState = 0;                            // message thread: the transport state the live state was last told about
    std::atomic<bool> muteChanged { false };               // a strip mute parameter changed on some thread; the poll tells the live state
    std::array<juce::RangedAudioParameter*, mu_limits::kMaxLayers> muteParams {};   // ch{N}_mute, cached (null = none)
    std::atomic<bool> fullPresetLoadFailed { false };   // set by a load that could not read its file

    // ─── The layer render contract (docs/mu-tronic/design.md 2.1; extraction stage E1) ────────────
    // Unused by a product until it adopts the contract: nothing below runs unless the product calls it,
    // except the bin's service passes (an empty bin frees nothing).
    //
    // The shared retire bin (Plugin/RetireBin.h). Declared before the swap poll, which services it, so the
    // poll is gone first on destruction; prepareForDestruction empties it before any product member goes.
    // Held on the heap (built with the processor, on the constructing thread) so ProcessorBase stays small
    // for hosts and tests that construct it on a stack.
    std::unique_ptr<mu_core::RetireBin> retireBinStorage = std::make_unique<mu_core::RetireBin>(layerAccess.epoch, layerAccess.layout);
    mu_core::RetireBin& retireBin = *retireBinStorage;
    // The count policy (RetireBin::CountPolicy) for tails past the live count. Constructor only.
    void setRetireCountPolicy(mu_core::RetireBin::CountPolicy p) noexcept { retireBin.setCountPolicy(p); }
    // Constructor only. On: the channel messages of a host block that renders nothing (a missed try-lock, an
    // edit in flight, 0 frames) are kept, up to kMaxCarriedMidiEvents, and handed to the next rendered block at
    // frame 0 ahead of its own events, with MidiView::carried counting them. For a product whose layers keep
    // input state (held keys): a key released during an edit would otherwise stay held. Off (the default): a
    // skipped block's MIDI is dropped, as a missed try-lock always dropped it.
    void setCarrySkippedMidi(bool on) noexcept { renderState.carryMidi = on; }

    // prepareToPlay (message or host thread, not processing; call it OUTSIDE layersLock): sizes the per-slot
    // scratch (kMaxLayers x 2 x blockSize) and the block's MIDI storage, prepares every slot's layer (spares
    // included), flushes the bin (cutAll then service) and resets the transport latches. Nothing it sizes is
    // ever grown on the audio thread: a longer host block is chunked.
    void prepareLayerRender(double sampleRate, int maxBlockSize);
    // releaseResources: flushes the bin and resets the latches.
    void releaseLayerRender();

    // Audio thread: the block's transport by the family rule (MIDI clock, host or own transport; see
    // TransportResolver.h), published for the next block's quantised control actions. `wrapBeats` bounds the
    // beat space (mu-Tant's 64-beat pattern space); 0 = unbounded. The bound travels in the result
    // (BlockTransport::beatWrap), which is what renderAndMix and the layers use.
    mu_core::BlockTransport resolveBlockTransport(juce::MidiBuffer& midi, int numSamples, double wrapBeats = 0.0);

    // Audio thread, noexcept: the base block driver (design 2.1.1). `buffer` is the host buffer AS RECEIVED: do
    // not call captureSidechainAndClear first. In order: (1) the host-block continuity check (every host block,
    // rendered or not); (2) one LayerBlock (try-lock or published view; a failed one clears `buffer` and renders
    // nothing); (3) one decode of `midi` into the block's MidiEvent storage; (4) chunks of at most the prepared
    // size: per chunk the DAW sidechain of that chunk's frames is captured, the chunk's output region cleared,
    // then renderLayers and the mixer (processCoreBlock) run on a sub-buffer, so an oversize host block never
    // overruns a prepared buffer and every chunk ducks against its own sidechain frames (C8). The layers' MIDI
    // out stays in the block's storage for mergeLayerMidiOut. A 0-frame block renders nothing and calls no hook;
    // its edges reach the next rendered block. The host's beat bound is t.beatWrap.
    void renderAndMix(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, const mu_core::BlockTransport& t,
                      std::array<juce::AudioBuffer<float>*, MixerEngine::MaxChannels>* directOuts = nullptr,
                      juce::AudioBuffer<float>* fxReturnsOut = nullptr) noexcept;

    // Audio thread, after renderAndMix: appends the block's layer MIDI out (the tails' owed note-offs first)
    // to `midi`, adding at most `byteBudget` bytes of MidiBuffer storage (midiEventBytes per event): the room the
    // caller knows the buffer already has, so it never grows on the audio thread. Frames are clamped to the last
    // rendered block. Returns the count added. (juce::MidiBuffer does not expose its allocated size, so the room
    // cannot be read from the buffer itself: the caller sizes it, e.g. with ensureSize on the message thread.)
    int mergeLayerMidiOut(juce::MidiBuffer& midi, int byteBudget) noexcept;
    // The MidiBuffer storage one short message of `size` bytes takes (int32 time + uint16 size + the bytes),
    // for a caller that reserves room for n events: n * midiEventBytes(3).
    static constexpr int midiEventBytes(int size) noexcept { return 6 + size; }

    // Audio thread, once per block after renderAndMix (also on a skipped block): flags the staged swaps whose
    // boundary passed, each slot by the loopBeats() renderLayers cached for it on the last rendered block, and a
    // staged full preset by `fullPresetLoopBeats` (the product's rule; default the 4/4 bar).
    void flagLayerSwapBoundaries(const mu_core::BlockTransport& t, double fullPresetLoopBeats = 4.0) noexcept
    {
        flagSwapBoundaries(t.playing, t.startBeat, t.blockBeats, [this, fullPresetLoopBeats](int slot) noexcept
        {
            if (slot == mu_hotswap::kFullPreset) return fullPresetLoopBeats;
            return slot >= 0 && slot < mu_limits::kMaxLayers ? renderState.loopBeats[(size_t) slot] : -1.0;
        });
    }

    // The audio thread's deferred-work request and the message-thread timer (100 Hz, one relaxed exchange per tick) that drains it.
    // The timer does nothing but check the flag. Nothing here uses AsyncUpdater: its trigger
    // allocates and takes a lock, so it must never run on the audio thread. Every destructor stops
    // the timer first so a tick cannot reach the virtual commitDeferredWork of a half-destroyed object.
    mu_hotswap::CommitMailbox swapMailbox;
    struct SwapPoll final : private juce::Timer
    {
        explicit SwapPoll(ProcessorBase& o) : owner(o) { startTimerHz(mu_core::MidiControlRouter::kDeferredWorkPollHz); }
        ~SwapPoll() override { stopTimer(); }
        void stop() { stopTimer(); }
        void timerCallback() override
        {
            owner.drainDeferredWork();
            owner.consumeMuteChange();
            owner.consumeTransportChange();
            if (owner.retireBin.hasPendingWork())
                owner.retireBin.service();   // a Done tail whose grace had not passed at the last pass
            if (owner.liveState.needsSettling())
                owner.settleLiveState();   // also catches a stopped-transport load, which never raises the mailbox
        }
        ProcessorBase& owner;
    } swapPoll { *this };

    // Stops the message-thread work this base owns (the swap poll), so nothing can call back into
    // a half-destroyed processor through a virtual (commitDeferredWork, getFullPresetExtension, ...).
    // Every product destructor calls it first, before any of its own members go; the base destructor
    // calls it again as a backstop. Safe to call more than once.
    // It also empties the retire bin (nothing is processing any more, so every tail is freed at once) while
    // the services a tail may read are still alive.
    void prepareForDestruction() noexcept
    {
        swapPoll.stop();
        retireBin.cutAll();
        retireBin.service(true);   // nothing processes again: even a tail still waiting for its store is freed
    }

    // Audio thread, once per block after the transport resolved: flag the staged swaps whose
    // boundary passed (`loopBeatsFor(slot)`, see LayerSwapper::flagBoundaries) and schedule the commit.
    template <typename LoopBeatsFor>
    void flagSwapBoundaries(bool isPlaying, double blockStartBeat, double blockBeats,
                            LoopBeatsFor&& loopBeatsFor) noexcept
    {
        if (layerSwapper.flagBoundaries(isPlaying, blockStartBeat, blockBeats, loopBeatsFor))
            requestDeferredWork();
    }

    // Products call this from prepareToPlay and releaseResources: the host is not driving
    // processBlock, so the swapper must not believe the transport still runs (loads apply at once).
    void resetSwapPlayState() noexcept { layerSwapper.resetPlayState(); }

    // The 4/4 bar-line rule shared by the products that step through layer presets.
    void flagSwapBoundaries(bool isPlaying, double blockStartBeat, double blockBeats) noexcept
    {
        if (layerSwapper.flagBoundaries(isPlaying, blockStartBeat, blockBeats))
            requestDeferredWork();
    }

    // Audio thread: ask the message thread to run the deferred work (swap commits, program changes,
    // control actions). An atomic flag, never an allocating message post; the swap poll timer drains
    // it within ~10 ms.
    void requestDeferredWork() noexcept { swapMailbox.request(); }

    // A new default layer for slot `slot`, fully built and prepared (it is published as is, under
    // the lock, so nothing may be left to allocate). Null when the product's layer count is fixed.
    virtual std::unique_ptr<Layer> createLayer(int /*slot*/) { return nullptr; }

    // Everything a layer-preset load does after the file is parsed: migrate, preload assets, then hand
    // the tree to layerSwapper.useSlot (applies now when stopped, commits at the boundary when playing).
    // `keepIdentity` is for products whose preset carries the layer's name and colour (mu-Clid).
    // Default: nothing. Callers use stageLayerPreset, never this directly.
    virtual void stageLayerPresetTree(int slot, juce::ValueTree tree, bool keepIdentity)
    { juce::ignoreUnused(slot, tree, keepIdentity); }

    // The one entry to the virtual above: a stage that is not a clip launch leaves no clip active.
    void stageLayerPreset(int slot, juce::ValueTree tree, bool keepIdentity, bool isClip)
    {
        if (! isClip)
            noteNonClipStage(slot);
        // Deep copy: the stage may migrate or consume its tree, and a store needs it as loaded.
        const auto staged = isClip ? juce::ValueTree() : tree.createCopy();
        stageLayerPresetTree(slot, std::move(tree), keepIdentity);
        notePendingPresetTree(slot, staged);
    }

    // Remembers a non-clip tree that is now waiting for its boundary (an invalid tree, a clip launch or an
    // at-once apply leaves nothing remembered).
    void notePendingPresetTree(int slot, const juce::ValueTree& staged);

    // Empties a layer's clip bank and activeClip, and forgets its live playing / queued clip (null-safe). The
    // shared structural edits call it; a product that overrides resetLayer without the shared reset calls it too.
    void clearLayerClips(int slot);

    // A non-clip layer-preset stage leaves no clip active, in the layer and in the live state.
    void noteNonClipStage(int slot);
    // Pushes each layer's clip occupancy to the live state (store, clear, file load, full-preset commit, edit).
    void publishClipMasks();
    // Message thread: the masks, then the settle of whatever the live state is waiting for.
    void settleLiveState() { publishClipMasks(); liveState.reconcile(); }
    // Message thread: tells the live state about a strip mute that changed since the last call (any thread may have changed it).
    // Message thread: tells the live state when the transport started, stopped or changed owner (Play button, host, clock).
    void consumeTransportChange()
    {
        const int state = (isInternalPlaying() ? 1 : 0) | (midiControlRouter.playIsOutside() ? 2 : 0);
        if (state == lastTransportState) return;
        lastTransportState = state;
        liveState.notifyTransportChanged();
    }
    void consumeMuteChange() { if (muteChanged.exchange(false, std::memory_order_relaxed)) liveState.notifyMuteChanged(); }

    virtual void applyMidiPresetLayer(int slot, const juce::File& f) { loadLayerPreset(slot, f); }
    virtual void applyFullMidiPreset(const juce::File& f)            = 0;

    // Number of active "channels" (rhythms / voices / whatever) — used to
    // gate per-slot PCs so we don't try to load into an inactive slot.
    // Default: getNumLayers(). Derived can override if the active count
    // differs from the total.
    virtual int getNumActiveLayers() const { return getNumLayers(); }


    // Sets the host BPM on the FX chain (tempo-synced FX) then calls
    // mixerEngine.processBlock(). Derived class calls this at the end of
    // processBlock() after triggers have fired and modulation has been applied.
    // `retired` (Stage 34) forwards the polyphonic-tail descriptor through to
    // the mixer's per-channel render phase — nullptr disables the feature.
    void processCoreBlock(juce::AudioBuffer<float>&                masterBus,
                          std::unique_ptr<VoiceEngine>*            voices,
                          int                                      numVoices,
                          int                                      numSamples,
                          double                                   effectiveBpm,
                          std::array<juce::AudioBuffer<float>*, MixerEngine::MaxChannels>* directOuts = nullptr,
                          juce::AudioBuffer<float>*                fxReturnsOut = nullptr,
                          const RetiredVoices*                     retired      = nullptr,
                          const MixerEngine::RenderChannelFn*      renderChannel = nullptr);

    // Capture the DAW sidechain input BEFORE the product clears its process buffer.
    // For an instrument the sidechain input bus can share buffer channels with the
    // main output (equal channel counts), so the buffer.clear() at the top of
    // processBlock would wipe the sidechain before the mixer reads it — producing no
    // ducking and a dead GR meter. Call this at the very top of processBlock, before
    // buffer.clear(). The mixer keeps a private copy (sized in MixerEngine::prepare),
    // so nothing allocates on the audio thread.
    void captureSidechainInput(juce::AudioBuffer<float>& buffer)
    {
        if (getBusCount(true) > 0)
            if (auto* scBus = getBus(true, 0); scBus != nullptr && scBus->isEnabled())
            {
                mixerEngine.copyExternalSidechain(getBusBuffer(buffer, true, 0));
                return;
            }
        mixerEngine.setExternalSidechain(nullptr, nullptr);
    }

    // Family block-start primitive: preserve the DAW sidechain, THEN clear the buffer.
    // Every product calls this single shared method as the first line of processBlock
    // (replacing a bare buffer.clear()), so a new sibling gets the correct behaviour for
    // free and can't reintroduce the "clear wipes the sidechain" bug.
    void captureSidechainAndClear(juce::AudioBuffer<float>& buffer)
    {
        captureSidechainInput(buffer);
        buffer.clear();
    }

protected:
    // Backing storage for getUiScale / setUiScale.
    float uiScale { kUiScaleMedium };

    // ─── App settings (family standard) ──────────────────────────────────────
    // The per-app settings file (<OS app data>/TDP/<appName>.xml) and the preferences every
    // product keeps there: UI size and MIDI-clock sync. Call once, early in the product's
    // constructor (before anything calls getContentDir); products read / write their own extra
    // keys through appSettings.
    void initAppSettings(const juce::String& appName);
    std::unique_ptr<juce::PropertiesFile> appSettings;
    juce::String  appName;
    MidiClockSync midiClockSync;

    // ─── Licensing (licensed products only) ──────────────────────────────────
    // A licensed product's identity: product id (the .lic `product=` field), licence file and
    // 32-byte Ed25519 public key (in the content folder), and the online-activation record file.
    struct LicensingConfig
    {
        const char*    productId;
        const char*    licenceFile;
        const uint8_t* publicKey;
        const char*    activationFile;
        const char*    checkoutUrl = nullptr;   // pay-what-you-want page the support prompt opens
    };
    // Check the offline licence + stored activation and wire activateOnlineFn. Call after
    // initAppSettings (it needs getContentDir). Defined in License/ProductLicensing.h — include
    // that only in a licensed product, which is the only kind that compiles the verifier.
    void initLicensing(const LicensingConfig& config);
    // Called after a successful online activation, OFF the message thread — e.g. to lift a
    // demo cap the product applied at startup.
    virtual void onActivated() {}
    bool licensingEnabled = false;
    bool offlineLicensed  = false;
    juce::String checkoutUrl;

    // ─── Mixer / global-FX parameters ────────────────────────────────────────
    // The channel-strip (ch{N}_*) and global FX ids synced to the engine via syncGlobalFxParam.
    // Each product calls startFxParamSync() once at the end of its constructor: the base then
    // listens to every FX id (the listener is removed in the base destructor) and seeds the
    // engines from the current values. After a state restore, products call syncAllFxParams()
    // because JUCE fires no change for values that are already set.
    static bool isFxParamId(const juce::String& id);
    void startFxParamSync();
    void syncAllFxParams();

    // ─── VST3 sidechain bus type ─────────────────────────────────────────────
    // JUCE maps the first input bus to Vst::kMain by default. For mu-family
    // plugins whose only input is a sidechain (no main audio input), kMain
    // prevents DAWs (e.g. Bitwig) from showing it as a routable sidechain pin.
    // Overriding getPluginHasMainInput() to return false tells JUCE to use
    // Vst::kAux instead, which DAWs recognise as a sidechain input.
    struct SidechainVST3Extensions final : public juce::VST3ClientExtensions
    {
        bool getPluginHasMainInput() const override { return false; }
    };
    SidechainVST3Extensions vst3Extensions;

public:
    juce::VST3ClientExtensions* getVST3ClientExtensions() override { return &vst3Extensions; }

#if MU_CORE_HAS_CLAP
    // CLAP parallel to getPluginHasMainInput()=false above: report that NO input port is the
    // main audio input, so the CLAP wrapper does not flag input port 0 as
    // CLAP_AUDIO_PORT_IS_MAIN. DAWs (Bitwig) then expose the lone input bus as a sidechain.
    bool isInputMain(int /*input*/) override { return false; }
#endif

private:
    // Parameter values of one layer slot, which sit under two prefixes: the product's own (from the
    // layer layout, e.g. v3_) and the mixer strip's (ch3_). Moving them is what makes the audible
    // strip and the knobs follow a moved layer; message thread only.
    void copyLayerParams (int from, int to);
    void swapLayerParams (int a, int b);
    void resetLayerParams(int slot);
    int  firstUnusedColourIndex() const;   // lowest palette index no live layer uses

    // syncGlobalFxParam dispatch helpers — one per ID family, so the public
    // entry point is a short prefix router rather than a ~100-line if/else chain.
    // syncMaster returns true when it handled the id. The channel/return helpers
    // take a raw `const char*` suffix pointing into the caller's id buffer (NOT a
    // juce::String substring) so the audio-thread automation path allocates nothing.
    void syncChannelStripParam(int channel, const char* param, float v);
    void syncReturnStripParam (int retIndex,  const char* rest,  float v);
    bool syncMasterParam      (const juce::String& id, float v);
    void syncFxSlotParam      (const juce::String& id, float v);

    // MIDI program-change queue: audio thread enqueues on incoming PC;
    // drainPendingMidiProgramChanges (message thread) drains and dispatches
    // to the virtual hooks.
    struct ProgramChangeEvent { int slot; int presetIndex; bool fullPreset; };
    static constexpr int kPCFifoSize = mu_limits::kProgramChangeFifoSize;
    juce::AbstractFifo                          pcFifo { kPCFifoSize };
    std::array<ProgramChangeEvent, kPCFifoSize> pcQueue {};

    // MIDI control mapping: the saved table, the audio hand-off and the gesture-holding writer.
    mu_core::MidiControlMap    midiControlMap;
    mu_core::MidiControlRouter midiControlRouter { midiControlMap };
    mu_core::ParamGestureHold  gestureHold;

private:
    // Forwards mixer / global-FX parameter changes to syncGlobalFxParam (see startFxParamSync).
    struct FxParamListener : juce::AudioProcessorValueTreeState::Listener
    {
        explicit FxParamListener(ProcessorBase& o) : owner(o) {}
        void parameterChanged(const juce::String& id, float v) override { owner.onFxParamChanged(id, v); }
        ProcessorBase& owner;
    };
    std::unique_ptr<FxParamListener> fxParamListener;
    // The listener's one entry (any thread, audio thread included): the engine sync, plus a flag for the
    // message thread when a strip mute moved. No allocation.
    void onFxParamChanged(const juce::String& id, float v);

    // ─── The block driver's state (ProcessorBase_Render.cpp) ─────────────────────────────────────
    // Audio-thread state survives blocks as values only, never as addresses of layers, views or tails.
    // Sized by prepareLayerRender; nothing here is allocated or grown on the audio thread.
    struct LayerRenderState
    {
        // The previous HOST block (rendered or not): the continuity check compares against it, so a block
        // after a missed lock or an edit is not a false jump (design 2.1.5, R5).
        // `frozen`: the host reported playing with a position that did not move (a stalled or pre-rolling host);
        // the blocks of a freeze are not jumps, and the one discontinuity is delivered when the position moves again.
        struct HostBlock { bool valid = false; bool playing = false; double startBeat = 0.0; double endBeat = 0.0;
                           double beatsPerSecond = 0.0; double blockSeconds = 0.0; bool frozen = false; };
        HostBlock host;
        bool   lastPlaying = false;     // the play state of the last RENDERED block (the edge latch)
        bool   jumpPending = false;     // sticky: a discontinuity since the last rendered block
        bool   edgeStarted = false, edgeStopped = false, edgeJumped = false;   // this block's edges (first chunk)

        double rate = 0.0;              // prepared sample rate
        int    prepared = 0;            // prepared block size: the longest call any layer or the mixer gets
        int    mixCount = 0;            // channels handed to the mixer this block (the count policy)

        std::array<juce::AudioBuffer<float>, mu_limits::kMaxLayers> scratch;   // per slot, 2 x prepared
        std::array<juce::AudioBuffer<float>, mu_limits::kMaxLayers> view;      // per-call views of scratch (no storage)
        std::array<mu_core::PeerTap, mu_limits::kMaxLayers> peers {};
        std::array<bool, mu_limits::kMaxLayers> live {};
        std::array<double, mu_limits::kMaxLayers> loopBeats {};                // cached per slot, last rendered block

        // The block's MIDI: input decoded at the front, the live layers' output after it, the note-off
        // reserve (tails' owed note-offs) in the last kMidiNoteOffReserve entries.
        juce::HeapBlock<mu_core::MidiEvent> midi;
        int32_t  midiIn = 0, liveOut = 0, tailOut = 0;
        uint16_t dropped = 0;
        // The carry (setCarrySkippedMidi): `carried` events of unrendered blocks wait at the front of `midi` for
        // the next rendered block; `midiCarried` is how many of this block's input events they are.
        bool     carryMidi = false;
        int32_t  carried = 0;
        uint16_t midiCarried = 0;
        uint16_t carryDropped = 0;      // channel messages the carry had no room for, reported with the next decode

        MixerEngine::RenderChannelFn copyToMixer;   // the mixer's channel r = scratch[r], an exact copy

        int lastBlockFrames = 0;        // frames of the last rendered host block (mergeLayerMidiOut clamps to it)
        int blocksRendered = 0, blocksSkipped = 0, largestCall = 0;   // test probe
    };
    std::unique_ptr<LayerRenderState> renderStateStorage = std::make_unique<LayerRenderState>();   // heap: see retireBinStorage
    LayerRenderState& renderState = *renderStateStorage;
    std::atomic<bool> useLayerRender { false };   // the test-controlled switch (see setUseLayerRenderForTest)

    void noteHostBlock(const mu_core::BlockTransport& t, int numSamples) noexcept;
    void noteSkippedBlock(const mu_core::BlockTransport& t, juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi) noexcept;
    void decodeBlockMidi(const juce::MidiBuffer& midi, int numSamples) noexcept;
    void captureSidechainRegion(juce::AudioBuffer<float>& buffer, int offset, int numSamples) noexcept;
    void renderLayers(const mu_core::LayerBlock& block, const mu_core::BlockTransport& t,
                      int offset, int numSamples, bool firstChunk, bool lastChunk) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProcessorBase)
};
