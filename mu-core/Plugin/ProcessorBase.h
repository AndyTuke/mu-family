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
#include "Persistence/LayerState.h"   // composed slot / full / host state (format 2)
#include "MuLimits.h"
#include "Plugin/HostTransport.h"
#include "Plugin/MidiClockSync.h"
#include "License/MachineFingerprint.h"
#include "License/OnlineActivation.h"   // OnlineActivationOutcome (decls only; .cpp is per-licensed-product)

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
class ProcessorBase : public juce::AudioProcessor,
                      protected juce::AsyncUpdater   // audio thread → message thread hand-off
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

    // ─── Channel metadata for shared mixer UI ────────────────────────────────
    // Each mu-family plugin has some N "channels" (rhythms in mu-clid; whatever
    // the trigger model dictates in mu-tant / mu-toni) — each with a display
    // name and a palette colour index. The shared MixerOverlay / MixerChannel
    // UI calls these to label channel strips, populate sidechain-source
    // dropdowns, etc., without needing to know what a channel actually IS.
    virtual int         getNumChannels()              const = 0;
    // The most channels the product can ever have (1..8) — sizes the program-change
    // channel table. Products with a fixed layer count return that count.
    virtual int         getMaxChannels()              const { return 8; }
    virtual juce::String getChannelName(int idx)       const = 0;
    virtual int         getChannelColourIndex(int idx) const = 0;

    // ─── MIDI program-change → preset loading ────────────────────────────────
    // The maps + FIFO + audio-thread scan + message-thread drain live here so
    // every mu-family plugin gets the same MIDI PC behaviour without
    // duplication. The plugin-specific bits are exposed as virtuals:
    //   - The per-slot preset directory + file extension (for the editor UI).
    //   - The full-preset directory + file extension (for the editor UI).
    //   - applyMidiPresetSlot / applyFullMidiPreset — how this plugin actually
    //     loads the file (mu-clid stages a rhythm preset / defers a full
    //     preset to the loop point; mu-tant will define its own semantics).
    // Public so the shared MIDI editor panels can read/write directly.
    MidiPresetMap     midiPresetMap;
    MidiFullPresetMap midiFullPresetMap;

    virtual juce::File   getPerSlotPresetDir()       const = 0;
    virtual juce::String getPerSlotPresetExtension() const = 0;
    virtual juce::File   getFullPresetDir()          const { return getPresetsDir(); }
    virtual juce::String getFullPresetExtension()    const = 0;

    // ─── Shell-facing API (overridden by each plugin) ────────────────────────
    // These are the surfaces the family's editor shell + TransportBar consume.
    // Defaults are no-ops returning sensible values so a young plugin (no
    // internal transport yet, no presets, no license gate) still builds and
    // shows the shell. mu-clid overrides every one of these against its real
    // implementations; mu-tant currently inherits the defaults for everything
    // except whatever it has implemented.

    // Internal transport (TransportBar play / BPM controls): a free-running clock each product
    // advances in processBlock while no host / MIDI clock drives it. Stopping resets the beat.
    virtual bool   isInternalPlaying()      const { return internalPlaying.load(std::memory_order_relaxed); }
    virtual void   toggleInternalPlay()
    {
        const bool now = ! internalPlaying.load(std::memory_order_relaxed);
        internalPlaying.store(now, std::memory_order_relaxed);
        if (! now) internalBeatPos.store(0.0, std::memory_order_relaxed);   // restart patterns cleanly
    }
    virtual double getInternalBpm()         const { return internalBpm.load(std::memory_order_relaxed); }
    virtual void   setInternalBpm(double bpm)     { internalBpm.store(juce::jlimit(20.0, 300.0, bpm), std::memory_order_relaxed); }
    virtual double getInternalBeatPos()     const { return internalBeatPos.load(std::memory_order_relaxed); }

    // Host transport as last seen by processBlock, for the UI (TransportBar). The UI must never
    // call getPlayHead() itself: the playhead is only valid on the audio thread during a block.
    bool isHostPlaying() const { return hostPlaying.load(std::memory_order_relaxed); }
    // Host meter (4/4 when the host gives none) and current bar start; false = no bar start.
    void getHostTimeSignature(int& numerator, int& denominator) const
    {
        const int packed = hostTimeSig.load(std::memory_order_relaxed);
        numerator   = packed >> 8;
        denominator = packed & 0xFF;
    }
    bool getHostBarStartPpq(double& ppq) const
    {
        const double v = hostBarStart.load(std::memory_order_relaxed);
        if (std::isnan(v)) return false;
        ppq = v;
        return true;
    }
    // Host beat position in quarter notes; false when the host supplied none.
    bool getHostPpqPosition(double& ppq) const
    {
        const double v = hostPpq.load(std::memory_order_relaxed);
        if (std::isnan(v)) return false;
        ppq = v;
        return true;
    }

    // The family bus layout: at most one sidechain input (stereo or disabled) and a stereo
    // main output. Products with extra output buses (mu-Clid's multi-out) override it.
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    // A host bypass still publishes the host transport, so the TransportBar keeps following
    // the DAW while the plugin is bypassed.
    using juce::AudioProcessor::processBlockBypassed;
    void processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        pollHostTransport();
        juce::AudioProcessor::processBlockBypassed(buffer, midi);
    }
    // Master loop — the global loop the shared mu-core MasterLoopSection displays
    // (and which products may use to gate preset/program-change swap timing). The
    // length lives in the `mstrLoop` APVTS param (0 = free; 1..16 → 16..256 steps,
    // 16 steps = 1 bar); products turn that into a step total + a live counter.
    // Default 0/off so a product without a master loop simply shows nothing.
    virtual int    getMasterLoopSteps()       const        { return 0; }
    virtual int    getMasterLoopCurrentStep() const        { return 0; }

    // MIDI clock sync (standalone) — shared by every product; the enable / messages choices
    // are saved in the app settings file.
    bool   getMidiSyncEnabled()  const { return midiClockSync.isEnabled(); }
    int    getMidiSyncMessages() const { return midiClockSync.getMessages(); }
    double getMidiClockBpm()     const { return midiClockSync.getBpm(); }
    bool   isMidiClockPlaying()  const { return midiClockSync.isPlaying(); }
    MidiClockSync::ClockState getMidiClockState() const { return midiClockSync.getClockState(); }
    void   setMidiSyncEnabled(bool on);
    void   setMidiSyncMessages(int mode);
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
    virtual juce::File         getPresetsDir()                                     const;
    virtual void               loadPreset(const juce::File& file);
    virtual void               savePreset(const juce::String& name,
                                           const juce::String& desc,
                                           const juce::String& cat,
                                           bool embedSamples);
    virtual juce::StringArray  loadCategoryList()                                  const;
    virtual const char*        getFullPresetTag()                                  const { return nullptr; }
    virtual juce::ValueTree    captureFullPreset()                                       { return apvts.copyState(); }
    virtual void               useLoadedFullPreset(juce::ValueTree /*state*/)            {}
    virtual void               ensureCategoryInList(const juce::String& /*cat*/)         {}
    virtual bool               hasPendingFullPreset()                              const { return false; }
    // Write the current state as a full preset at exactly `file` (headless render --save-preset).
    // Default: the standard wrapped preset; false (after onLoadError) when it can't be written.
    virtual bool               saveFullPresetTo(const juce::File& file);

    // Headless render: the render loop never returns to the message loop, so triggerAsyncUpdate()
    // is never serviced. Call after each processBlock so deferred work (hot-swap commits, MIDI
    // program changes) runs on the calling thread. No-op when nothing is pending.
    void flushPendingAsyncUpdates() { handleUpdateNowIfNeeded(); }

    // Default preset: <presets>/_default.<ext>, written by the shell's "Save as Default". Each
    // product calls loadStartupDefault() at the end of its constructor so a saved default is
    // restored on launch (a host session restore then replaces it). Render mode sets
    // skipAutoLoadDefault first so headless renders start from the factory state.
    juce::File         getDefaultPresetFile() const;
    virtual void       loadDefaultPreset();
    void               loadStartupDefault() { if (! skipAutoLoadDefault) loadDefaultPreset(); }
    inline static bool skipAutoLoadDefault = false;

    // Per-slot (layer) presets — one rhythm / voice / layer / lane. The shared header bar and the
    // MIDI program-change slots drive these; the product implements save / load / reset. A load
    // while playing is staged for the slot's boundary (hasPendingSwap until it lands).
    // onSlotPresetCommitted fires on the message thread once a slot preset has been applied, so
    // the editor can refresh what APVTS attachments don't cover. The editor must clear it in its
    // destructor (the processor can outlive the editor).
    virtual juce::Array<juce::File> slotPresetFiles(int slot) const;   // default: every file in the per-slot dir
    virtual void saveSlotPreset(int /*slot*/, const juce::String& /*name*/) {}
    virtual void loadSlotPreset(int /*slot*/, const juce::File& /*file*/)   {}
    virtual void resetSlot(int /*slot*/)                                    {}
    virtual bool hasPendingSwap(int /*slot*/) const                         { return false; }
    std::function<void(int slot)> onSlotPresetCommitted;

    // Set by the standalone (mu-link bridge) so the product can publish its current full-preset
    // name for display on the mu-link mixer. Null in plugin builds / when mu-link isn't wired.
    // Products call publishPresetName(...) from their loadPreset override.
    std::function<void(const juce::String&)> onPresetNameChanged;
    void publishPresetName(const juce::String& name) { if (onPresetNameChanged) onPresetNameChanged(name); }

    // Content directory — where preset / sample-library / keybindings folders live:
    // Documents/TDP/<appName> (the name given to initAppSettings), or a folder the user
    // picked (saved as "contentDir"). Invalid before initAppSettings; the shell tolerates that.
    virtual juce::File getContentDir() const;

    // License gate. A product with no licensing model (it never calls initLicensing) is always
    // licensed, so it gets the full editor and no demo banner. A licensed product calls
    // initLicensing; then a Release build is licensed when the offline signed .lic verifies OR
    // the machine is online-activated, and a Debug / tester build always runs unlocked.
    bool hasLicensing() const noexcept { return licensingEnabled; }   // initLicensing was called
    virtual bool isLicensed() const
    {
       #if MUFAMILY_REQUIRE_LICENSE
        return ! licensingEnabled || offlineLicensed || isOnlineActivated();
       #else
        return true;
       #endif
    }

    // ─── Demo-mode caps (consulted ONLY when !isLicensed()) ──────────────────
    // Max "channels" (rhythms / voices / layers) the unlicensed editor allows.
    // Default: no limit, so a licensed build — or a product with no demo tier —
    // is unrestricted. A product with a demo tier overrides this (e.g. 1).
    virtual int demoMaxChannels() const { return std::numeric_limits<int>::max(); }
    // Max sequencer steps a pattern may have in demo (e.g. 16). Default: no limit.
    virtual int demoMaxSteps() const { return std::numeric_limits<int>::max(); }

    // Single source for the shell/product UI to gate demo-restricted affordances:
    //   - canAddChannel:      is the add-rhythm/voice control allowed right now?
    //   - canSaveLayerPreset: may a per-layer preset (.muRhythm/.muPattern) be saved?
    // Full-preset save + the demo banner are already gated on isLicensed() directly
    // in EditorShellBase; per-layer save is blocked in demo to match.
    bool canAddChannel()      const { return isLicensed() || getNumChannels() < demoMaxChannels(); }
    bool canSaveLayerPreset() const { return isLicensed(); }
    //   - maxSteps:           a step-count limit of `fullMax`, cut to demoMaxSteps() in demo.
    int  maxSteps(int fullMax) const  { return isLicensed() ? fullMax : juce::jmin(fullMax, demoMaxSteps()); }

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
    static constexpr float kUiScaleMedium = 1.0f;
    static constexpr float kUiScaleLarge  = 1.25f;
    float getUiScale() const noexcept { return uiScale; }
    void  setUiScale(float scale);

    // ─── Shell callbacks (message-thread, registered by the editor) ──────────
    // Editor MUST clear these in its dtor — processor can outlive the editor
    // when a DAW keeps the plugin loaded after closing the window. Any deferred
    // invocation into a destroyed editor is a UAF.
    std::function<void(std::function<void()>)>       onSaveAndQuit;
    std::function<void(const juce::String& message)> onLoadError;
    std::function<void()>                            onPresetSwapCommitted;
    std::function<void(float)>                       onUiScaleChanged;

    // Audio-thread: call once per processBlock. Queues the block's program changes
    // (scanMidiProgramChanges) and, if any, schedules the message-thread drain that
    // loads them via applyMidiPresetSlot / applyFullMidiPreset.
    void queueMidiProgramChanges(const juce::MidiBuffer& midi)
    {
        if (scanMidiProgramChanges(midi))
            triggerAsyncUpdate();
    }

    // Audio-thread: scans incoming MIDI for program-change messages on
    // channels 1-8 (per-slot map, gated by `midiPresetMap.getChannelMask()`)
    // and channel 9 (full-preset map, gated by `midiFullPresetMap.isEnabled()`).
    // Each matching PC is enqueued into the lock-free FIFO. Returns true if
    // any PCs were enqueued. Products call queueMidiProgramChanges instead.
    bool scanMidiProgramChanges(const juce::MidiBuffer& midi);

    // Message-thread: drains the FIFO and dispatches each event via the
    // applyMidiPresetSlot / applyFullMidiPreset virtuals. Run by handleAsyncUpdate.
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
    // non-parameter data), then layer presets, full presets and host sessions all go through these,
    // so a slot is written and applied by the same code wherever it appears.
    void initSlotState(const juce::StringArray& slotPrefixes, mu_pp::LayerExtras extras)
    {
        slotLayout = mu_pp::LayerLayout(*this, slotPrefixes);
        slotExtras = std::move(extras);
    }
    juce::ValueTree captureComposedState()           { return mu_pp::captureState(apvts.state.getType(), slotLayout, slotExtras); }
    void            applyComposedState(const juce::ValueTree& state) { mu_pp::applyState(state, slotLayout, slotExtras); }
    juce::ValueTree captureSlotNode(int slot, const juce::Identifier& type) { return mu_pp::captureSlot(slotLayout, slotExtras, slot, type); }
    void            applySlotNode(int slot, const juce::ValueTree& node)    { mu_pp::applySlot(slotLayout, slotExtras, slot, node); }
    // A state in the composed shape: an older APVTS-dump state is rebuilt (`otherChild` receives
    // the product's older root children); a composed one is returned as is.
    juce::ValueTree toComposedState(const juce::ValueTree& state,
                                    const std::function<void(const juce::ValueTree&, juce::ValueTree&)>& otherChild = {}) const
    {
        return mu_pp::composeLegacyState(state, slotLayout, otherChild);
    }
    mu_pp::LayerLayout slotLayout;
    mu_pp::LayerExtras slotExtras;

protected:
    // Internal transport state. Written by the UI (play / BPM) and the audio thread (beat
    // advance); relaxed atomics — each is a lone published value.
    std::atomic<bool>   internalPlaying { false };
    std::atomic<double> internalBeatPos { 0.0 };
    std::atomic<double> internalBpm     { 120.0 };

    // Reads the host playhead for this block and publishes it for the UI. Call it from
    // processBlock in place of mu_core::readHostTransport(getPlayHead()) — audio thread only.
    mu_core::HostTransport pollHostTransport()
    {
        const auto t = mu_core::readHostTransport(getPlayHead());
        hostPlaying.store(t.playing, std::memory_order_relaxed);
        hostPpq.store(t.hasPosition ? t.ppqPosition : std::numeric_limits<double>::quiet_NaN(),
                      std::memory_order_relaxed);
        hostTimeSig.store((juce::jlimit(1, 255, t.timeSigNumerator) << 8) | juce::jlimit(1, 255, t.timeSigDenominator),
                          std::memory_order_relaxed);
        hostBarStart.store(t.hasBarStart ? t.barStartPpq : std::numeric_limits<double>::quiet_NaN(),
                           std::memory_order_relaxed);
        return t;
    }

private:
    // Host transport published by pollHostTransport(); NaN position = none supplied. One value
    // per atomic so the UI never sees a torn position/has-position pair.
    std::atomic<bool>   hostPlaying { false };
    std::atomic<double> hostPpq     { std::numeric_limits<double>::quiet_NaN() };
    std::atomic<int>    hostTimeSig { (4 << 8) | 4 };   // numerator << 8 | denominator, one value so it can't tear
    std::atomic<double> hostBarStart { std::numeric_limits<double>::quiet_NaN() };
    static_assert(std::atomic<double>::is_always_lock_free, "the UI reads atomic<double> transport values");
protected:
    // Deferred message-thread work first (product hot-swap commits), then program changes.
    void handleAsyncUpdate() final
    {
        commitDeferredWork();
        drainPendingMidiProgramChanges();
    }

    // Message-thread work the audio thread asked for with triggerAsyncUpdate()
    // (e.g. hot-swap commits at a loop boundary). Runs before queued program
    // changes are loaded. Default: nothing.
    virtual void commitDeferredWork() {}

    virtual void applyMidiPresetSlot(int slot, const juce::File& f) { loadSlotPreset(slot, f); }
    virtual void applyFullMidiPreset(const juce::File& f)            = 0;

    // Number of active "channels" (rhythms / voices / whatever) — used to
    // gate per-slot PCs so we don't try to load into an inactive slot.
    // Default: getNumChannels(). Derived can override if the active count
    // differs from the total (e.g. mu-clid's `numActiveRhythms` atomic
    // tracks runtime-active rhythms separately from the static channel count).
    virtual int getNumActiveChannels() const { return getNumChannels(); }


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

private:
    // Forwards mixer / global-FX parameter changes to syncGlobalFxParam (see startFxParamSync).
    struct FxParamListener : juce::AudioProcessorValueTreeState::Listener
    {
        explicit FxParamListener(ProcessorBase& o) : owner(o) {}
        void parameterChanged(const juce::String& id, float v) override { owner.syncGlobalFxParam(id, v); }
        ProcessorBase& owner;
    };
    std::unique_ptr<FxParamListener> fxParamListener;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProcessorBase)
};
