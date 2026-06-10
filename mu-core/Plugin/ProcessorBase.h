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
#include "MuLimits.h"

#include <array>
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
class ProcessorBase : public juce::AudioProcessor
#if MU_CORE_HAS_CLAP
                    , public clap_juce_extensions::clap_juce_audio_processor_capabilities
#endif
{
public:
    ProcessorBase(const BusesProperties& props,
                  juce::AudioProcessorValueTreeState::ParameterLayout layout,
                  const juce::Identifier& stateTreeType = juce::Identifier("MuFamilyState"));
    ~ProcessorBase() override = default;

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
    virtual juce::File   getFullPresetDir()          const = 0;
    virtual juce::String getFullPresetExtension()    const = 0;

    // ─── Shell-facing API (overridden by each plugin) ────────────────────────
    // These are the surfaces the family's editor shell + TransportBar consume.
    // Defaults are no-ops returning sensible values so a young plugin (no
    // internal transport yet, no presets, no license gate) still builds and
    // shows the shell. mu-clid overrides every one of these against its real
    // implementations; mu-tant currently inherits the defaults for everything
    // except whatever it has implemented.

    // Internal transport (TransportBar play/BPM controls; standalone-only DAWs
    // typically defer to the host playhead instead).
    virtual bool   isInternalPlaying()      const          { return false; }
    virtual void   toggleInternalPlay()                    {}
    virtual double getInternalBpm()         const          { return 120.0; }
    virtual void   setInternalBpm(double /*bpm*/)          {}
    virtual double getInternalBeatPos()     const          { return 0.0; }

    // MIDI clock sync (standalone).
    virtual bool   getMidiSyncEnabled()     const          { return false; }
    virtual int    getMidiSyncMessages()    const          { return 0; }
    virtual double getMidiClockBpm()        const          { return 120.0; }
    virtual bool   isMidiClockPlaying()     const          { return false; }

    // Presets — directory, save/load, and the shared category list. Default
    // returns yield a usable "no presets yet" UI state in the transport bar.
    virtual juce::File         getPresetsDir()                                     const { return {}; }
    virtual void               loadPreset(const juce::File& /*file*/)                    {}
    virtual void               savePreset(const juce::String& /*name*/,
                                           const juce::String& /*desc*/,
                                           const juce::String& /*cat*/,
                                           bool /*embedSamples*/)                        {}
    virtual juce::StringArray  loadCategoryList()                                  const { return {}; }
    virtual void               ensureCategoryInList(const juce::String& /*cat*/)         {}
    virtual bool               hasPendingFullPreset()                              const { return false; }

    // Content directory — where preset / sample-library / keybindings folders
    // live. Returned File can be invalid; the shell tolerates that.
    virtual juce::File getContentDir() const { return {}; }

    // License gate — true by default so products without a licensing model
    // (mu-tant currently) get the full editor instead of a demo banner.
    virtual bool isLicensed() const { return true; }

    // ─── UI scale (Medium baseline, Large 1.25x) ─────────────────────────────
    // Storage lives on the base so every plugin inherits the same scale handling
    // out of the box. Derived classes typically override setUiScale to persist
    // through their appSettings PropertiesFile before delegating to the base.
    static constexpr float kUiScaleMedium = 1.0f;
    static constexpr float kUiScaleLarge  = 1.25f;
    virtual float getUiScale() const noexcept { return uiScale; }
    virtual void  setUiScale(float scale)
    {
        const float clamped = juce::jlimit(kUiScaleMedium, kUiScaleLarge, scale);
        if (uiScale == clamped) return;
        uiScale = clamped;
        if (onUiScaleChanged) onUiScaleChanged(clamped);
    }

    // ─── Shell callbacks (message-thread, registered by the editor) ──────────
    // Editor MUST clear these in its dtor — processor can outlive the editor
    // when a DAW keeps the plugin loaded after closing the window. Any deferred
    // invocation into a destroyed editor is a UAF.
    std::function<void(std::function<void()>)>       onSaveAndQuit;
    std::function<void(const juce::String& message)> onLoadError;
    std::function<void()>                            onPresetSwapCommitted;
    std::function<void(float)>                       onUiScaleChanged;

    // Audio-thread: scans incoming MIDI for program-change messages on
    // channels 1-8 (per-slot map, gated by `midiPresetMap.getChannelMask()`)
    // and channel 9 (full-preset map, gated by `midiFullPresetMap.isEnabled()`).
    // Each matching PC is enqueued into the lock-free FIFO. Returns true if
    // any PCs were enqueued — caller should then `triggerAsyncUpdate()`
    // (the AsyncUpdater is owned by the derived processor; ProcessorBase
    // doesn't inherit it so the derived class keeps full control of its
    // async lifecycle for hot-swap etc.).
    bool scanMidiProgramChanges(const juce::MidiBuffer& midi);

    // Message-thread: drains the FIFO and dispatches each event via the
    // applyMidiPresetSlot / applyFullMidiPreset virtuals. Call from the
    // derived processor's handleAsyncUpdate().
    void drainPendingMidiProgramChanges();

    // Maps a shared global-FX / return / master / channel-strip APVTS parameter
    // to fxChain + mixerEngine state. Products route the matching IDs here from
    // their parameterChanged (the param set is declared by mu_mixfx::addGlobalFxParams
    // + the product's `ch{i}_*` strip). Handles: `ch{i}_*`, `ret_*`, `mstr_lvl/pan`,
    // `mst_ins*`, `eff_*`, `eff2*`, `dly_*`, `rev_*`, `echo_*`. Unrecognised IDs no-op.
    void syncGlobalFxParam(const juce::String& id, float v);

protected:
    virtual void applyMidiPresetSlot(int slot, const juce::File& f) = 0;
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
    // Backing storage for the default getUiScale / setUiScale. Derived classes
    // typically also persist the value through their own appSettings file.
    float uiScale { kUiScaleMedium };

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
    // syncMaster returns true when it handled the id.
    void syncChannelStripParam(int channel, const juce::String& param, float v);
    void syncReturnStripParam (int retIdx,  const juce::String& rest,  float v);
    bool syncMasterParam      (const juce::String& id, float v);
    void syncFxSlotParam      (const juce::String& id, float v);

    // MIDI program-change queue: audio thread enqueues on incoming PC;
    // drainPendingMidiProgramChanges (message thread) drains and dispatches
    // to the virtual hooks.
    struct ProgramChangeEvent { int slot; int presetIndex; bool fullPreset; };
    static constexpr int kPCFifoSize = mu_limits::kProgramChangeFifoSize;
    juce::AbstractFifo                          pcFifo { kPCFifoSize };
    std::array<ProgramChangeEvent, kPCFifoSize> pcQueue {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProcessorBase)
};
