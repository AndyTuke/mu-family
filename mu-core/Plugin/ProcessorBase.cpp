#include "Plugin/ProcessorBase.h"
#include "Plugin/MixerFxParams.h"           // mu_mixfx::isGlobalFxParamId
#include "Sequencer/ClipClipboard.h"         // the process-wide clip clipboard
#include "Persistence/PresetFiles.h"         // mu_pp full-preset files
#include "Audio/FX/Slots/FXAlgorithmDef.h"   // FXAlgorithmRegistry
#include "Audio/AlgorithmNames.h"             // mu_audio::kInsertAlgorithmCount
#include <cstring>                            // std::strcmp — alloc-free suffix compare
#include <map>
#include <memory>
#include <mutex>

#if MU_CORE_HAS_CLAP
 #pragma message("mu-core: ProcessorBase built WITH CLAP sidechain capability (isInputMain=false)")
#endif

ProcessorBase::ProcessorBase(const BusesProperties& props,
                             juce::AudioProcessorValueTreeState::ParameterLayout layout,
                             const juce::Identifier& stateTreeType)
    : ProcessorCore(props),
      apvts(*this, nullptr, stateTreeType, std::move(layout))
{
    // The block driver's mixer hook: channel r is slot r's scratch, copied exactly (raw pointers, so a
    // buffer's "cleared" flag can never turn the copy into a clear). A slot that nothing wrote this call (its
    // view still carries the cleared flag) is not copied: the mixer's channel stays cleared, as a voice that
    // renders nothing leaves it, so the mixer skips it exactly as before and adds no zeros (which would turn a
    // later channel's -0 into +0). Built once here: no allocation per block.
    renderState.copyToMixer = [this](int r, juce::AudioBuffer<float>& buf, int n)
    {
        if (r < 0 || r >= mu_limits::kMaxLayers) return;
        if (renderState.view[(size_t) r].hasBeenCleared()) return;
        const auto& src = renderState.scratch[(size_t) r];
        const int nCh = juce::jmin(buf.getNumChannels(), src.getNumChannels());
        const int len = juce::jmin(n, src.getNumSamples());
        for (int c = 0; c < nCh; ++c)
            juce::FloatVectorOperations::copy(buf.getWritePointer(c), src.getReadPointer(c), len);
    };

    // Performance mode's live state: how a performance item is staged and cancelled is this processor's
    // own full-preset path, so the state itself never names a product or the hot-swap machinery.
    mu_core::LiveState::Callbacks cb;
    cb.supportsPerformanceMode = [this] { return isStandaloneApp(); };
    cb.canLoad = [this](int item)
    {
        return liveState.getPerformance().status(item, getFullPresetDir(), apvts.state.getType().toString())
                   == mu_pp::PerformanceFile::ItemStatus::Ok;
    };
    cb.stageItem = [this](int item)
    {
        // A load that could not read the file stages nothing and says so through noteFullPresetLoadFailed();
        // warnings on a load that did succeed (a missing sample, dropped modulators) are not failures.
        fullPresetLoadFailed.store(false, std::memory_order_relaxed);
        applyFullMidiPreset(liveState.getPerformance().resolve(item, getFullPresetDir()));
        if (fullPresetLoadFailed.load(std::memory_order_relaxed)) return mu_core::LiveState::Stage::Failed;
        return hasPendingFullPreset() ? mu_core::LiveState::Stage::Queued   // waiting for its boundary
                                      : mu_core::LiveState::Stage::Applied; // nothing left staged: applied at once (stopped)
    };
    cb.fullPending = [this] { return hasPendingFullPreset(); };
    cb.stageSerial = [this] { return fullStageSerial(); };
    cb.fullCommitCount = [this] { return fullCommitCount(); };
    cb.cancelItem = [this] { cancelPendingFullPreset(); };
    cb.layerCount = [this] { return getNumLayers(); };
    cb.layerPresent = [this](int l) { return ! layersAreSlots() || slotTypeId(l) != mu_core::LayerTypeId::Empty; };
    cb.layerColourIndex = [this](int layer) { return layer >= 0 && layer < getNumLayers() ? getLayerColourIndex(layer) : 0; };

    // The clip bank's live half: a launch is this processor's own launchClip, a mute the MuteLayer action.
    cb.launchClip   = [this](int layer, int clip) { return launchClip(layer, clip); };
    cb.beginStageBatch = [this] { layerSwapper.beginBatch(); };
    cb.endStageBatch   = [this] { layerSwapper.endBatch(); };
    cb.markSceneSlot   = [this](int layer) { layerSwapper.markScene(layer); };
    cb.preloadClip     = [this](int layer, int clip)
    {
        if (auto* l = getLayer(layer); l != nullptr && l->clips->isOccupied(clip)) l->preloadStaged(l->clips->get(clip));
    };
    cb.slotPending  = [this](int layer) { return hasPendingSwap(layer); };
    cb.slotSerial   = [this](int layer) { return slotStageSerial(layer); };
    cb.cancelSlot   = [this](int layer) { cancelPendingSwap(layer); };
    cb.muteLayer    = [this](int layer)
    {
        mu_core::ControlAction mute;
        mute.type  = mu_core::ControlActionType::MuteLayer;
        mute.layer = layer;
        return perform(mute);
    };
    for (int i = 0; i < mu_limits::kMaxLayers; ++i)
        midiInRaw[(size_t) i] = apvts.getRawParameterValue("ch" + juce::String(i) + "_midiIn");
    for (int i = 0; i < mu_limits::kMaxLayers; ++i)
        muteParams[(size_t) i] = apvts.getParameter("ch" + juce::String(i) + "_mute");   // cached: the lamps ask per pad per frame
    cb.layerMuted   = [this](int layer)
    {
        const auto* p = layer >= 0 && layer < mu_limits::kMaxLayers ? muteParams[(size_t) layer] : nullptr;
        return p != nullptr && p->getValue() > 0.5f;
    };
    cb.savedClip    = [this](int layer)
    {
        const auto* l = getLayer(layer);
        return l != nullptr && l->clips->isOccupied(l->activeClip) ? l->activeClip : -1;
    };
    cb.clipPlaying  = [this](int layer, int clip)
    {
        if (auto* l = getLayer(layer)) l->activeClip = clip;   // the saved shadow of the live playing clip
    };
    cb.clipFitsLayer = [this](int layer, int clip)
    {
        const auto* l = getLayer(layer);
        return l == nullptr || ! layersAreSlots() || ! l->clips->isOccupied(clip)
            || layerPresetType(l->clips->get(clip)) == slotTypeId(layer);   // the same test launchClip makes
    };
    cb.clipboardEmpty   = [] { return ! mu_core::clipClipboard().hasClip(); };
    cb.canPasteClip     = [this](int layer) { return canPasteClip(layer); };
    cb.hasSceneControls = [this] { return offersSceneControls(); };
    cb.hasLayerPages    = [this] { return offersLayerPages(); };
    cb.transportPlaying = [this] { return isInternalPlaying(); };
    cb.transportOutside = [this] { return midiControlRouter.playIsOutside(); };
    liveState.setCallbacks(std::move(cb));
}

// Performs one control action (message thread): a knob / button parameter, the transport, or a
// layer's mute / solo. Returns false for actions not supported yet or whose target is gone.
// rt-ok-fn: message thread; reached from MidiControlRouter::drain (SwapPoll timer), UI sinks and LiveState, never from processBlock
bool ProcessorBase::perform(const mu_core::ControlAction& a)
{
    using T = mu_core::ControlActionType;

    // The layer strip's own mute / solo parameter, flipped.
    auto flipStrip = [this, &a](const char* rest)
    {
        if (a.layer < 0 || a.layer >= getNumLayers()) return false;
        auto* p = apvts.getParameter("ch" + juce::String(a.layer) + "_" + rest);
        if (p == nullptr) return false;
        gestureHold.set(*p, p->getValue() > 0.5f ? 0.0f : 1.0f);
        return true;
    };

    switch (a.type)
    {
        case T::Parameter:
        {
            auto* p = apvts.getParameter(a.paramId);
            if (p == nullptr) return false;   // the parameter is gone: the mapping stays, unresolved
            gestureHold.set(*p, a.value);
            return true;
        }
        case T::MuteLayer:       return flipStrip("mute");
        case T::MuteSelectedLayer:
        {
            // The same mute as pressing the layer's strip, on whichever layer Performance mode has selected.
            auto mute = a;
            mute.type  = T::MuteLayer;
            mute.layer = liveState.getSelectedLayer();
            return perform(mute);
        }
        case T::SoloLayer:       return flipStrip("solo");
        case T::TransportToggle:
            if (midiControlRouter.playIsOutside()) return false;   // the host / clock owns play
            toggleInternalPlay();
            return true;
        case T::TransportPlay:
        case T::TransportStop:
            if (midiControlRouter.playIsOutside()) return false;
            if (isInternalPlaying() != (a.type == T::TransportPlay))
                toggleInternalPlay();
            return true;
        case T::SelectLayer:
        case T::PerformanceMode:
        case T::PerformanceItem:
        case T::PerformancePrev:
        case T::PerformanceNext:
        case T::AppPrev:
        case T::AppNext:
        case T::PagePrev:
        case T::PageNext:
        case T::LaunchClip:
        case T::LaunchScene:
        case T::ClipNext:
        case T::ClipPrev:
            // Auto-save is off while the mode is on, so what was edited before it is put in the bank first.
            if (a.type == T::PerformanceMode) flushClipAutoSave();
            return liveState.perform(a);
        case T::StoreClip:
        case T::ClearClip:
        case T::CopyClip:
        case T::CutClip:
        case T::PasteClip:
        {
            // Screen only: these touch the bank or the clipboard, so the locked Performance mode refuses them and no map can name them.
            if (liveState.isPerformanceMode()) return false;
            if (hasPendingFullPreset()) return false;   // a staged full preset replaces the layers and their banks
            if (a.type == T::ClearClip) return clearClip(a.layer, a.index);
            if (a.type == T::CopyClip)  return copyClip(a.layer, a.index);
            if (a.type == T::CutClip)   return cutClip(a.layer, a.index);
            if (a.type == T::PasteClip) return pasteClip(a.layer, a.index);
            if (! storeClip(a.layer, a.index)) return false;
            liveState.clipStored(a.layer, a.index);
            return true;
        }
        default:                 return false;   // presets, panic: later
    }
}

ProcessorBase::~ProcessorBase()
{
    prepareForDestruction();   // backstop: the product destructors already ran it first
    fxParamSync.stop();   // the listener must go before the members it touches
}

// ─── Online activation and MIDI program changes: forwarders to ActivationWorker and ProgramChangeRouter ──────────
bool ProcessorBase::activateOnlineAsync(const juce::String& key,
                                        std::function<void(const mu_core::OnlineActivationOutcome&)> done)
{
    return activationWorker.start(activateOnlineFn, key, std::move(done));
}

bool ProcessorBase::scanMidiProgramChanges(const juce::MidiBuffer& midi)
{
    return programChangeRouter.scan(midi, midiPresetMap, midiFullPresetMap);
}

void ProcessorBase::drainPendingMidiProgramChanges()
{
    programChangeRouter.drain(getNumActiveLayers(), midiPresetMap, midiFullPresetMap,
                              [this](const juce::File& f) { applyFullMidiPreset(f); },
                              [this](int slot, const juce::File& f) { applyMidiPresetLayer(slot, f); });
}

// ─── Mixer / global-FX parameters: forwarders to FxParamSync ──────────────────────────────────────────────────
bool ProcessorBase::isFxParamId(const juce::String& id)     { return mu_core::FxParamSync::isFxParamId(id); }
void ProcessorBase::startFxParamSync()                       { fxParamSync.start(); }
void ProcessorBase::syncAllFxParams()                        { fxParamSync.syncAll(); }
void ProcessorBase::syncGlobalFxParam(const juce::String& id, float v) { fxParamSync.syncGlobal(id, v); }

void ProcessorBase::processCoreBlock(juce::AudioBuffer<float>&                masterBus,
                                     std::unique_ptr<VoiceEngine>*            voices,
                                     int                                      numVoices,
                                     int                                      numSamples,
                                     double                                   effectiveBpm,
                                     std::array<juce::AudioBuffer<float>*, MixerEngine::MaxChannels>* directOuts,
                                     juce::AudioBuffer<float>*                fxReturnsOut,
                                     const RetiredVoices*                     retired,
                                     const MixerEngine::RenderChannelFn*      renderChannel)
{
    fxChain.setHostBpm(effectiveBpm);
    mixerEngine.processBlock(masterBus, numVoices, voices, fxChain, numSamples,
                             directOuts, fxReturnsOut, retired, renderChannel);
}

bool ProcessorBase::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Sidechain input: at most one, stereo or disabled.
    const auto& ins = layouts.inputBuses;
    if (ins.size() > 1) return false;
    if (ins.size() == 1 && ins.getReference(0) != juce::AudioChannelSet::stereo()
                        && ins.getReference(0) != juce::AudioChannelSet::disabled())
        return false;
    // Main output: stereo.
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}
