#include "Plugin/ProcessorBase.h"
#include "Plugin/MixerFxParams.h"           // mu_mixfx::isGlobalFxParamId
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
    cb.layerColourIndex = [this](int layer) { return layer >= 0 && layer < getNumLayers() ? getLayerColourIndex(layer) : 0; };

    // The clip bank's live half: a launch is this processor's own launchClip, a mute the MuteLayer action.
    cb.launchClip   = [this](int layer, int clip) { return launchClip(layer, clip); };
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
    cb.transportPlaying = [this] { return isInternalPlaying(); };
    cb.transportOutside = [this] { return midiControlRouter.playIsOutside(); };
    liveState.setCallbacks(std::move(cb));
}

bool ProcessorBase::scanMidiProgramChanges(const juce::MidiBuffer& midi)
{
    const uint8_t chMask           = midiPresetMap.getChannelMask();
    const bool    fullPresetEnabled = midiFullPresetMap.isEnabled();
    if (chMask == 0 && ! fullPresetEnabled) return false;

    bool needPC = false;
    for (const auto meta : midi)
    {
        // Raw bytes, not a juce::MidiMessage: constructing one allocates for messages over 8 bytes (SysEx).
        if (meta.numBytes < 2 || meta.numBytes > 3 || (meta.data[0] & 0xf0) != 0xc0) continue;
        const int ch = (meta.data[0] & 0x0f) + 1;      // 1-based, 1..16

        int  slot   = -1;
        bool isFull = false;
        if (ch >= 1 && ch <= 8)
        {
            if (! (chMask & (1 << (ch - 1)))) continue;
            slot = ch - 1;
        }
        else if (ch == MidiFullPresetMap::Channel)
        {
            if (! fullPresetEnabled) continue;
            isFull = true;
        }
        else
        {
            continue;
        }

        int start1, size1, start2, size2;
        pcFifo.prepareToWrite(1, start1, size1, start2, size2);
        if (size1 + size2 > 0)
        {
            const int dst = (size1 > 0) ? start1 : start2;
            pcQueue[(size_t) dst] = { slot, meta.data[1] & 0x7f, isFull };
            pcFifo.finishedWrite(1);
            needPC = true;
        }
    }
    return needPC;
}

void ProcessorBase::drainPendingMidiProgramChanges()
{
    const int ready = pcFifo.getNumReady();
    if (ready <= 0) return;

    int start1, size1, start2, size2;
    pcFifo.prepareToRead(ready, start1, size1, start2, size2);
    const int activeChannels = getNumActiveLayers();

    auto handle = [this, activeChannels](const ProgramChangeEvent& ev)
    {
        if (ev.fullPreset)
        {
            if (! midiFullPresetMap.hasPreset(ev.presetIndex)) return;
            const juce::File f { midiFullPresetMap.getPresetPath(ev.presetIndex) };
            if (f.existsAsFile())
                applyFullMidiPreset(f);
            return;
        }
        if (ev.slot < 0 || ev.slot >= activeChannels) return;
        if (! midiPresetMap.hasPreset(ev.presetIndex))   return;
        const juce::File f { midiPresetMap.getPresetPath(ev.presetIndex) };
        if (f.existsAsFile())
            applyMidiPresetLayer(ev.slot, f);
    };
    for (int i = 0; i < size1; ++i) handle(pcQueue[(size_t)(start1 + i)]);
    for (int i = 0; i < size2; ++i) handle(pcQueue[(size_t)(start2 + i)]);
    pcFifo.finishedRead(ready);
}

// Performs one control action (message thread): a knob / button parameter, the transport, or a
// layer's mute / solo. Returns false for actions not supported yet or whose target is gone.
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
        case T::LaunchClip:
        case T::ClipNext:
        case T::ClipPrev:
            return liveState.perform(a);
        case T::StoreClip:
        case T::ClearClip:
        {
            // Screen only: these write the bank, so the locked Performance mode refuses them and no map can name them.
            if (liveState.isPerformanceMode()) return false;
            if (hasPendingFullPreset()) return false;   // a staged full preset replaces the layers and their banks
            if (a.type == T::ClearClip) return clearClip(a.layer, a.index);
            if (! storeClip(a.layer, a.index)) return false;
            liveState.clipStored(a.layer, a.index);
            return true;
        }
        default:                 return false;   // presets, panic: later
    }
}

namespace
{
// The test storage root (empty when unset). A function-local so there is no static-init order issue.
juce::File& storageRootOverride()
{
    static juce::File root;
    return root;
}
}

ProcessorBase::ScopedStorageRoot::ScopedStorageRoot(const juce::File& root)
    : previous(storageRootOverride())
{
    storageRootOverride() = root;
}

ProcessorBase::ScopedStorageRoot::~ScopedStorageRoot()
{
    storageRootOverride() = previous;
}

namespace
{
// One PropertiesFile per settings file per module: every instance in the host process shares it, so a
// second instance's write can no longer revert a setting the first one saved (each used to hold its own
// copy, loaded at construction and rewritten whole). PropertiesFile serialises its own accessors.
std::shared_ptr<juce::PropertiesFile> sharedSettingsFile(const juce::File& file, const juce::PropertiesFile::Options& opts)
{
    static std::mutex registryLock;
    static std::map<juce::String, std::weak_ptr<juce::PropertiesFile>> registry;

    const std::lock_guard<std::mutex> guard(registryLock);
    auto& slot = registry[file.getFullPathName()];
    if (auto existing = slot.lock()) return existing;
    auto created = std::make_shared<juce::PropertiesFile>(file, opts);
    slot = created;
    return created;
}
}

void ProcessorBase::initAppSettings(const juce::String& name)
{
    appName = name;
    juce::PropertiesFile::Options opts;
    opts.applicationName     = name;
    opts.filenameSuffix      = "xml";
    opts.folderName          = "TDP";
    opts.osxLibrarySubFolder = "Application Support";
    const auto& testRoot = storageRootOverride();
    auto settingsFile = testRoot != juce::File()
                          ? testRoot.getChildFile("settings").getChildFile(name + ".xml")
                          : opts.getDefaultFile();
    settingsFile.getParentDirectory().createDirectory();
    appSettings = sharedSettingsFile(settingsFile, opts);

    // The MIDI control map is per user and per product, beside the settings file.
    midiControlMap.setStorageFile(settingsFile.getParentDirectory().getChildFile(name + "_midiControl.json"));
    midiControlMap.load();

    // Restore the shared preferences: a fresh open uses the last UI size + MIDI-clock choice.
    uiScale = juce::jlimit(kUiScaleMedium, kUiScaleLarge,
                           (float) appSettings->getDoubleValue("uiScale", (double) kUiScaleMedium));
    midiClockSync.setEnabled (appSettings->getBoolValue("midiSyncEnabled",  false));
    midiClockSync.setMessages(appSettings->getIntValue ("midiSyncMessages", 2));
    syncOffsetMs.store(juce::jlimit(kSyncOffsetMinMs, kSyncOffsetMaxMs, appSettings->getIntValue("syncOffsetMs", 0)));
    setTimeSignature(appSettings->getIntValue("timeSigNumerator", 4), appSettings->getIntValue("timeSigDenominator", 4));
}

juce::File ProcessorBase::getContentDir() const
{
    if (appSettings != nullptr)
        if (const auto stored = appSettings->getValue("contentDir"); stored.isNotEmpty())
            return juce::File(stored);
    if (appName.isEmpty()) return {};
    if (const auto& testRoot = storageRootOverride(); testRoot != juce::File())
        return testRoot.getChildFile("content").getChildFile(appName);
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
               .getChildFile("TDP").getChildFile(appName);
}

void ProcessorBase::setUiScale(float scale)
{
    const float clamped = juce::jlimit(kUiScaleMedium, kUiScaleLarge, scale);
    if (uiScale == clamped) return;
    if (appSettings != nullptr) { appSettings->setValue("uiScale", (double) clamped); appSettings->saveIfNeeded(); }
    uiScale = clamped;
    if (onUiScaleChanged) onUiScaleChanged(clamped);
}

void ProcessorBase::setMidiSyncEnabled(bool on)
{
    midiClockSync.setEnabled(on);
    if (appSettings != nullptr) { appSettings->setValue("midiSyncEnabled", on); appSettings->saveIfNeeded(); }
}

void ProcessorBase::setMidiSyncMessages(int mode)
{
    midiClockSync.setMessages(mode);
    if (appSettings != nullptr) { appSettings->setValue("midiSyncMessages", mode); appSettings->saveIfNeeded(); }
}

void ProcessorBase::setSyncOffsetMs(int ms)
{
    ms = juce::jlimit(kSyncOffsetMinMs, kSyncOffsetMaxMs, ms);
    syncOffsetMs.store(ms, std::memory_order_relaxed);
    if (appSettings != nullptr) { appSettings->setValue("syncOffsetMs", ms); appSettings->saveIfNeeded(); }
}

void ProcessorBase::setTimeSignature(int numerator, int denominator)
{
    // x/4 and x/8 only (the beat is a quarter or an eighth), 1..16 beats a bar.
    denominator = denominator == 8 ? 8 : 4;
    numerator   = juce::jlimit(1, 16, numerator);
    timeSignature.store((numerator << 8) | denominator, std::memory_order_relaxed);
    if (appSettings != nullptr)
    {
        appSettings->setValue("timeSigNumerator", numerator);
        appSettings->setValue("timeSigDenominator", denominator);
        appSettings->saveIfNeeded();
    }
}

bool ProcessorBase::isFxParamId(const juce::String& id)
{
    return id.startsWith("ch") || mu_mixfx::isGlobalFxParamId(id);
}

namespace
{
// For an id of the form ch{N}_{suffix} (N one or two digits), writes N to `index` and returns a pointer to the
// suffix inside the id's own buffer; otherwise returns nullptr. Allocation-free (audio-thread automation path).
const char* channelStripSuffix(const juce::String& id, int& index) noexcept
{
    const char* s = id.toRawUTF8();
    if (s[0] != 'c' || s[1] != 'h' || s[2] < '0' || s[2] > '9') return nullptr;
    int n = s[2] - '0';
    const char* p = s + 3;
    if (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); ++p; }
    if (*p != '_' || p[1] == '\0') return nullptr;
    index = n;
    return p + 1;
}
}

void ProcessorBase::onFxParamChanged(const juce::String& id, float v)
{
    syncGlobalFxParam(id, v);
    // ch{N}_mute: a flag for the message-thread poll (the lamps follow a mute made anywhere); alloc-free.
    int stripIdx = -1;
    if (const char* suffix = channelStripSuffix(id, stripIdx); suffix != nullptr && std::strcmp(suffix, "mute") == 0)
        muteChanged.store(true, std::memory_order_relaxed);
}

namespace
{
// One online-activation call off the message thread. Owned and joined by the processor, so `fn` may capture it.
struct ActivationWorker : juce::Thread
{
    using Done = std::function<void(const mu_core::OnlineActivationOutcome&)>;

    ActivationWorker(std::function<mu_core::OnlineActivationOutcome(const juce::String&)> f, juce::String k, Done d,
                     std::shared_ptr<std::atomic<bool>> aliveFlag)
        : juce::Thread("mu activation"), fn(std::move(f)), key(std::move(k)), done(std::move(d)), alive(std::move(aliveFlag)) {}

    void run() override
    {
        const auto outcome = fn(key);   // blocks on the network here
        if (threadShouldExit()) return;
        // The flag is only lowered on the message thread (prepareForDestruction), where this runs too.
        juce::MessageManager::callAsync([alive = alive, done = done, outcome]
        {
            if (alive->load(std::memory_order_acquire) && done) done(outcome);
        });
    }

    std::function<mu_core::OnlineActivationOutcome(const juce::String&)> fn;
    juce::String key;
    Done done;
    std::shared_ptr<std::atomic<bool>> alive;
};

constexpr int kActivationJoinMs = 12000;   // longer than the activation request's own 8 s connection timeout
}

bool ProcessorBase::activateOnlineAsync(const juce::String& key,
                                        std::function<void(const mu_core::OnlineActivationOutcome&)> done)
{
    if (! activateOnlineFn) return false;
    if (activationWorker != nullptr)
    {
        if (activationWorker->isThreadRunning()) return false;
        activationWorker.reset();   // a finished one: nothing to wait for
    }
    activationWorker = std::make_unique<ActivationWorker>(activateOnlineFn, key, std::move(done), activationAlive);
    activationWorker->startThread();
    return true;
}

void ProcessorBase::stopActivationWorker() noexcept
{
    activationAlive->store(false, std::memory_order_release);   // a reply already queued is dropped
    if (activationWorker != nullptr)
    {
        activationWorker->stopThread(kActivationJoinMs);
        activationWorker.reset();
    }
}

ProcessorBase::~ProcessorBase()
{
    prepareForDestruction();   // backstop: the product destructors already ran it first
    if (fxParamListener != nullptr)
        for (auto* p : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
                if (isFxParamId(rp->getParameterID()))
                    apvts.removeParameterListener(rp->getParameterID(), fxParamListener.get());
}

// Listen to every mixer / global-FX parameter, then seed the engines from the current values.
void ProcessorBase::startFxParamSync()
{
    jassert(fxParamListener == nullptr);   // once per processor
    fxParamListener = std::make_unique<FxParamListener>(*this);
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
            if (isFxParamId(rp->getParameterID()))
                apvts.addParameterListener(rp->getParameterID(), fxParamListener.get());
    syncAllFxParams();   // JUCE doesn't fire parameterChanged on construction
}

void ProcessorBase::syncAllFxParams()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
        {
            const auto id = rp->getParameterID();
            if (isFxParamId(id))
                if (auto* a = apvts.getRawParameterValue(id))
                    syncGlobalFxParam(id, a->load());
        }
}

void ProcessorBase::syncGlobalFxParam(const juce::String& id, float v)
{
    // Prefix router → per-family helper. Keeps each automation tick a single
    // prefix test plus one bounded if/else inside the matching helper.
    int stripIdx = -1;
    if (const char* suffix = channelStripSuffix(id, stripIdx); suffix != nullptr)   // ch{N}_{param}
    {
        if (stripIdx < MixerEngine::MaxChannels)
            syncChannelStripParam(stripIdx, suffix, v);   // suffix into id buffer, no alloc
        return;
    }

    if constexpr (MixerEngine::kNumBuses > 0)
        if (id.startsWith("bus"))                                           // bus{B}_{param}, bus{B}_i{I}_{param}
        {
            syncBusParam(id, v);
            return;
        }

    if (id.startsWith("ret_"))                                              // ret_{eff|dly|rev}_{param}
    {
        int retIndex = -1;
        if      (id.startsWith("ret_eff_")) retIndex = 0;
        else if (id.startsWith("ret_dly_")) retIndex = 1;
        else if (id.startsWith("ret_rev_")) retIndex = 2;
        if (retIndex >= 0)
            syncReturnStripParam(retIndex, id.toRawUTF8() + 8, v);   // suffix into id buffer, no alloc
        return;
    }

    if (syncMasterParam(id, v)) return;                                     // mstr_*, mst_ins*

    syncFxSlotParam(id, v);                                                 // eff_*, eff2*, dly_*, rev_*, echo_*
}

// param / rest are raw NUL-terminated suffixes pointing into the caller's id
// buffer (param IDs are pure ASCII). std::strcmp avoids building any juce::String,
// so this stays allocation-free on the audio-thread automation path.
void ProcessorBase::syncChannelStripParam(int channel, const char* param, float v)
{
    auto& ch = mixerEngine.channels[(size_t) channel];
    auto is = [param](const char* s) { return std::strcmp(param, s) == 0; };
    if      (is("lvl"))     ch.level.store(v, std::memory_order_relaxed);
    else if (is("pan"))     ch.pan.store(v, std::memory_order_relaxed);
    else if (is("mute"))    ch.mute.store(v > 0.5f, std::memory_order_relaxed);
    else if (is("solo"))    ch.solo.store(v > 0.5f, std::memory_order_relaxed);
    else if (is("sendEff")) ch.sendEffect.store(v, std::memory_order_relaxed);
    else if (is("sendDly")) ch.sendDelay.store(v, std::memory_order_relaxed);
    else if (is("sendRev")) ch.sendReverb.store(v, std::memory_order_relaxed);
    else if (is("scSrc"))   ch.sidechainSource.store(juce::roundToInt(v) - 1, std::memory_order_relaxed);
    else if (is("scAmt"))   ch.sidechainAmount.store(v, std::memory_order_relaxed);
    else if (is("scAtk"))   ch.sidechainAttackMs.store(v, std::memory_order_relaxed);
    else if (is("scRel"))   ch.sidechainReleaseMs.store(v, std::memory_order_relaxed);
    else if (is("outBus"))  ch.outputBus.store(juce::jlimit(0, 8, juce::roundToInt(v)), std::memory_order_relaxed);
    else if (is("bus"))     ch.busAssign.store(juce::jlimit(0, MixerEngine::kNumBuses, juce::roundToInt(v)), std::memory_order_relaxed);
}

// bus{B}_{suffix}: the strip's own suffixes, or i{I}_{drvChar|insP1..4} for an insert. Pointer arithmetic on the
// id's buffer only, so the automation path allocates nothing.
void ProcessorBase::syncBusParam(const juce::String& id, float v)
{
    const char* s = id.toRawUTF8();
    if (s[3] < '0' || s[3] > '9') return;
    const int b = s[3] - '0';
    if (b >= MixerEngine::kNumBuses || s[4] != '_') return;
    const char* rest = s + 5;
    auto& bus = mixerEngine.buses[(size_t) b];

    if (rest[0] == 'i' && rest[1] >= '0' && rest[1] <= '9' && rest[2] == '_')   // i{I}_{param}
    {
        const int i = rest[1] - '0';
        if (i >= MixerEngine::kBusInserts) return;
        auto& ins = bus.insert[i];
        const char* param = rest + 3;
        auto is = [param](const char* t) { return std::strcmp(param, t) == 0; };
        if      (is("drvChar")) ins.insertAlgo.store(juce::jlimit(0, mu_audio::kInsertAlgorithmCount - 1, juce::roundToInt(v)), std::memory_order_relaxed);
        else if (is("insP1"))   ins.insertParam[0].store(v, std::memory_order_relaxed);
        else if (is("insP2"))   ins.insertParam[1].store(v, std::memory_order_relaxed);
        else if (is("insP3"))   ins.insertParam[2].store(v, std::memory_order_relaxed);
        else if (is("insP4"))   ins.insertParam[3].store(v, std::memory_order_relaxed);
        return;
    }

    auto is = [rest](const char* t) { return std::strcmp(rest, t) == 0; };
    if      (is("lvl"))     bus.level.store(v, std::memory_order_relaxed);
    else if (is("pan"))     bus.pan.store(v, std::memory_order_relaxed);
    else if (is("mute"))    bus.mute.store(v > 0.5f, std::memory_order_relaxed);
    else if (is("solo"))    bus.solo.store(v > 0.5f, std::memory_order_relaxed);
    else if (is("sendEff")) bus.sendEffect.store(v, std::memory_order_relaxed);
    else if (is("sendDly")) bus.sendDelay.store(v, std::memory_order_relaxed);
    else if (is("sendRev")) bus.sendReverb.store(v, std::memory_order_relaxed);
    else if (is("scSrc"))   bus.sidechainSource.store(juce::jlimit(0, mu_mixfx::kScSrcMax, juce::roundToInt(v)) - 1, std::memory_order_relaxed);
    else if (is("scAmt"))   bus.sidechainAmount.store(v, std::memory_order_relaxed);
    else if (is("scAtk"))   bus.sidechainAttackMs.store(v, std::memory_order_relaxed);
    else if (is("scRel"))   bus.sidechainReleaseMs.store(v, std::memory_order_relaxed);
}

void ProcessorBase::syncReturnStripParam(int retIndex, const char* rest, float v)
{
    auto& ret = mixerEngine.returns[(size_t) retIndex];
    auto is = [rest](const char* s) { return std::strcmp(rest, s) == 0; };
    if      (is("lvl"))   ret.level.store(v, std::memory_order_relaxed);
    else if (is("pan"))   ret.pan.store(v, std::memory_order_relaxed);
    else if (is("mute"))  ret.mute.store(v > 0.5f, std::memory_order_relaxed);
    else if (is("solo"))  ret.solo.store(v > 0.5f, std::memory_order_relaxed);
    else if (is("scSrc")) ret.sidechainSource.store(juce::jlimit(0, mu_mixfx::kScSrcMax, (int) v) - 1, std::memory_order_relaxed);
    else if (is("scAmt")) ret.sidechainAmount.store(juce::jlimit(0.0f, 1.0f, v), std::memory_order_relaxed);
    else if (is("scAtk")) ret.sidechainAttackMs.store(v, std::memory_order_relaxed);
    else if (is("scRel")) ret.sidechainReleaseMs.store(v, std::memory_order_relaxed);
}

bool ProcessorBase::syncMasterParam(const juce::String& id, float v)
{
    if (id == "mstr_lvl")     { mixerEngine.masterLevel.store(v, std::memory_order_relaxed); return true; }
    if (id == "mstr_pan")     { mixerEngine.masterPan.store(v, std::memory_order_relaxed);   return true; }
    if (id == "mst_insChar")  { mixerEngine.masterInsert1Algo.store(juce::jlimit(0, mu_audio::kInsertAlgorithmCount - 1, (int) v), std::memory_order_relaxed); return true; }
    if (id == "mst_insP1")    { mixerEngine.masterInsert1Param[0].store(v, std::memory_order_relaxed); return true; }
    if (id == "mst_insP2")    { mixerEngine.masterInsert1Param[1].store(v, std::memory_order_relaxed); return true; }
    if (id == "mst_insP3")    { mixerEngine.masterInsert1Param[2].store(v, std::memory_order_relaxed); return true; }
    if (id == "mst_insP4")    { mixerEngine.masterInsert1Param[3].store(v, std::memory_order_relaxed); return true; }
    if (id == "mst_ins2Char") { mixerEngine.masterInsert2Algo.store(juce::jlimit(0, mu_audio::kInsertAlgorithmCount - 1, (int) v), std::memory_order_relaxed); return true; }
    if (id == "mst_ins2P1")   { mixerEngine.masterInsert2Param[0].store(v, std::memory_order_relaxed); return true; }
    if (id == "mst_ins2P2")   { mixerEngine.masterInsert2Param[1].store(v, std::memory_order_relaxed); return true; }
    if (id == "mst_ins2P3")   { mixerEngine.masterInsert2Param[2].store(v, std::memory_order_relaxed); return true; }
    if (id == "mst_ins2P4")   { mixerEngine.masterInsert2Param[3].store(v, std::memory_order_relaxed); return true; }
    return false;
}

void ProcessorBase::syncFxSlotParam(const juce::String& id, float v)
{
    auto& eff = fxChain.effectSlot();
    auto& dly = fxChain.delaySlot();
    auto& rev = fxChain.reverbSlot();
    auto readDenom = [this](const char* p) {
        static const int denoms[] = { 32, 16, 8, 4 };
        auto* a = apvts.getRawParameterValue(p);
        return denoms[juce::jlimit(0, 3, a ? (int) a->load() : 0)];
    };
    auto readBool = [this](const char* p) {
        auto* a = apvts.getRawParameterValue(p); return a && a->load() > 0.5f;
    };

    if      (id == "eff_algo")
    {
        eff.setAlgorithm((int) v);

        // The new algorithm starts from its own member defaults while eff_p0..4 still hold the old algorithm's
        // knob values (a host automating the algorithm leaves them alone), so the knobs and the sound disagree.
        // Push the knob values into the new algorithm. The editor's algorithm menu rewrites the knobs itself
        // right after this, which then overrides what is pushed here.
        static constexpr const char* kEffKnobIds[5] = { "eff_p0", "eff_p1", "eff_p2", "eff_p3", "eff_p4" };
        const auto& algos = FXAlgorithmRegistry::effectAlgorithms();
        const int ai = eff.getAlgorithmIndex();
        if (ai >= 0 && ai < (int) algos.size())
        {
            const auto& params = algos[(size_t) ai].params;
            for (int i = 0; i < (int) params.size() && i < 5; ++i)
                if (auto* knob = apvts.getRawParameterValue(kEffKnobIds[i]))
                    eff.setParam(params[(size_t) i].id,
                                 params[(size_t) i].minVal + knob->load() * (params[(size_t) i].maxVal - params[(size_t) i].minVal));
        }
    }
    else if (id == "eff_en")   eff.setEnabled(v > 0.5f);
    else if (id.startsWith("eff_p"))
    {
        const int idx = id.getTrailingIntValue();   // "eff_pN" → N, alloc-free (no substring)
        const auto& algos = FXAlgorithmRegistry::effectAlgorithms();
        const int ai = eff.getAlgorithmIndex();
        if (ai < (int) algos.size() && idx < (int) algos[(size_t) ai].params.size())
        {
            const auto& pd = algos[(size_t) ai].params[(size_t) idx];
            eff.setParam(pd.id, pd.minVal + v * (pd.maxVal - pd.minVal));
        }
    }
    else if (id == "dly_en")   dly.setEnabled(v > 0.5f);
    else if (id == "dly_mode") dly.setTimeMode(v > 0.5f ? DelaySlot::TimeMode::Sync : DelaySlot::TimeMode::Free);
    else if (id == "dly_ms")   dly.setDelayMs(v);
    else if (id == "dly_syncDenom" || id == "dly_syncDot" || id == "dly_syncTrip")
        dly.setTimeDivision(readDenom("dly_syncDenom"), readBool("dly_syncDot"), readBool("dly_syncTrip"));
    else if (id == "dly_count")  dly.setTimeCount(juce::jmax(1, (int) v));
    else if (id == "dly_fb")     dly.setFeedback(v);
    else if (id == "dly_spread") dly.setSpread(v);
    else if (id == "dly_dirt")   dly.setDirt(v);
    else if (id == "rev_algo")
    {
        rev.setAlgorithm((int) v);   // applies the algorithm's preset (size, damp, diffusion, mod, pre-delay)

        // As with the effect: a host automating the algorithm leaves the knobs where they were, so put the
        // knob values back into the slot. The editor's algorithm menu then rewrites the knobs from the preset.
        struct KnobToSlot { const char* apvtsId; const char* slotId; };
        static constexpr KnobToSlot kRevKnobs[] = { { "rev_size", "size" }, { "rev_decay", "decay" }, { "rev_pre", "predelay" },
                                                    { "rev_diff", "diffusion" }, { "rev_damp", "damp" },
                                                    { "rev_mod", "mod" } };
        for (const auto& k : kRevKnobs)
            if (auto* knob = apvts.getRawParameterValue(k.apvtsId))
                rev.setParam(k.slotId, knob->load());
    }
    else if (id == "rev_en")     rev.setEnabled(v > 0.5f);
    else if (id == "rev_lvl")    rev.setLevel(v);
    else if (id == "rev_size")   rev.setParam("size",      v);
    else if (id == "rev_decay")  rev.setParam("decay",     v);
    else if (id == "rev_pre")    rev.setParam("predelay",  v);
    else if (id == "rev_diff")   rev.setParam("diffusion", v);
    else if (id == "rev_damp")   rev.setParam("damp",      v);
    else if (id == "rev_mod")    rev.setParam("mod",       v);
    else if (id == "rev_dirt")   rev.setParam("dirt",      v);
    else if (id == "eff2dly")    fxChain.setEffectToDelaySend(v);
    else if (id == "eff2rev")    fxChain.setEffectToReverbSend(v);
    else if (id == "dly2rev")    fxChain.setDelayToReverbSend(v);
    else if (id == "echo_en")    eff.getEchoDelay().setEnabled(v > 0.5f);
    else if (id == "echo_mode")  eff.getEchoDelay().setTimeMode(v > 0.5f ? DelaySlot::TimeMode::Sync : DelaySlot::TimeMode::Free);
    else if (id == "echo_ms")    eff.getEchoDelay().setDelayMs(v);
    else if (id == "echo_syncDenom" || id == "echo_syncDot" || id == "echo_syncTrip")
        eff.getEchoDelay().setTimeDivision(readDenom("echo_syncDenom"), readBool("echo_syncDot"), readBool("echo_syncTrip"));
    else if (id == "echo_count")  eff.getEchoDelay().setTimeCount(juce::jmax(1, (int) v));
    else if (id == "echo_fb")     eff.getEchoDelay().setFeedback(v);
    else if (id == "echo_spread") eff.getEchoDelay().setSpread(v);
    else if (id == "echo_dirt")   eff.getEchoDelay().setDirt(v);
}

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

void ProcessorBase::savePreset(const juce::String& name, const juce::String& desc,
                               const juce::String& category, bool /*embedSamples*/)
{
    if (const char* tag = getFullPresetTag())
        mu_pp::writeFullPreset(getPresetsDir(), getFullPresetExtension(), tag,
                               name, desc, category, captureFullPreset(), onLoadError);
}

bool ProcessorBase::saveFullPresetTo(const juce::File& file)
{
    const char* tag = getFullPresetTag();
    if (tag == nullptr) return false;
    return mu_pp::writeFullPreset(file.getParentDirectory(), getFullPresetExtension(), tag,
                                  file.getFileNameWithoutExtension(), {}, {}, captureFullPreset(), onLoadError)
           != juce::File();
}

juce::File ProcessorBase::getPresetsDir() const
{
    const auto content = getContentDir();
    return content == juce::File() ? juce::File() : content.getChildFile("Presets");
}

juce::Array<juce::File> ProcessorBase::slotPresetFiles(int /*slot*/) const
{
    return mu_pp::listPresetFiles(getLayerPresetDir(), getLayerPresetExtension());
}

juce::File ProcessorBase::presetDirOf(const mu_core::LayerTypeInfo& info) const
{
    const auto content = getContentDir();
    if (content == juce::File() || juce::String(info.contentFolderName).isEmpty() || juce::String(info.presetFolder).isEmpty())
        return {};
    return content.getSiblingFile(info.contentFolderName).getChildFile(info.presetFolder);
}

juce::String ProcessorBase::presetExtensionOf(const mu_core::LayerTypeInfo& info)
{
    return juce::String(info.presetExtension).trimCharactersAtStart(".");
}

mu_core::LayerTypeId ProcessorBase::slotTypeId(int slot) const
{
    const auto* layer = getLayer(slot);
    return layer != nullptr ? (mu_core::LayerTypeId) layer->typeId() : mu_core::LayerTypeId::Empty;
}

juce::File ProcessorBase::typedLayerPresetDir(int slot) const
{
    const auto* info = layerTypes.find(slotTypeId(slot));
    return info != nullptr ? presetDirOf(*info) : juce::File();
}

juce::String ProcessorBase::typedLayerPresetExtension(int slot) const
{
    const auto* info = layerTypes.find(slotTypeId(slot));
    return info != nullptr ? presetExtensionOf(*info) : juce::String();
}

juce::Array<juce::File> ProcessorBase::typedSlotPresetFiles(int slot) const
{
    juce::Array<juce::File> out;
    auto addType = [&](const mu_core::LayerTypeInfo& info)
    {
        for (const auto& f : mu_pp::listPresetFiles(presetDirOf(info), presetExtensionOf(info)))
        {
            // Types that share an extension: keep the files that name this type (an unreadable file names none).
            if (info.typeFromPreset != nullptr)
            {
                const auto xml = juce::XmlDocument::parse(f);
                if (xml == nullptr || info.typeFromPreset(juce::ValueTree::fromXml(*xml)) != info.id) continue;
            }
            out.addIfNotAlreadyThere(f);
        }
    };
    const auto* own = layerTypes.find(slotTypeId(slot));
    if (own != nullptr)
        addType(*own);
    else
        for (int t = 0; t < layerTypes.size(); ++t) addType(layerTypes.at(t));
    return out;
}

mu_core::LayerTypeId ProcessorBase::layerPresetType(const juce::ValueTree& preset, const juce::String& extension) const
{
    auto found = mu_core::LayerTypeId::Empty;
    int matches = 0;
    for (int t = 0; t < layerTypes.size(); ++t)
    {
        const auto& info = layerTypes.at(t);
        if (extension.isNotEmpty() && ! presetExtensionOf(info).equalsIgnoreCase(extension.trimCharactersAtStart("."))) continue;
        if (juce::String(info.presetTag).isNotEmpty() && ! preset.hasType(info.presetTag)) continue;
        if (info.typeFromPreset != nullptr && info.typeFromPreset(preset) != info.id) continue;
        found = info.id;
        ++matches;
    }
    return matches == 1 ? found : mu_core::LayerTypeId::Empty;
}

void ProcessorBase::saveTypedLayerPreset(int slot, const juce::String& name)
{
    const auto* info = layerTypes.find(slotTypeId(slot));
    if (info == nullptr || juce::String(info->presetTag).isEmpty()) return;   // an Empty slot has nothing to save
    const auto dir = presetDirOf(*info);
    if (dir == juce::File()) return;
    if (auto xml = captureLayerNode(slot, info->presetTag).createXml())
        mu_pp::writeXmlAtomically(*xml, dir.getChildFile(mu_pp::safePresetFileName(name, info->name) + "." + presetExtensionOf(*info)),
                                  onLoadError);
}

void ProcessorBase::loadTypedLayerPreset(int slot, const juce::File& file)
{
    if (getLayer(slot) == nullptr || ! file.existsAsFile()) return;
    const auto xml = juce::XmlDocument::parse(file);
    const auto tree = xml != nullptr ? juce::ValueTree::fromXml(*xml) : juce::ValueTree();
    const auto type = tree.isValid() ? layerPresetType(tree, file.getFileExtension()) : mu_core::LayerTypeId::Empty;
    const auto* fileInfo = layerTypes.find(type);
    if (fileInfo == nullptr)
    {
        if (onLoadError) onLoadError("Could not read \"" + file.getFileName() + "\"");
        return;
    }

    const auto slotType = slotTypeId(slot);
    if (slotType == mu_core::LayerTypeId::Empty)
    {
        // An Empty slot takes the file's type, with the file's rows as the new layer's first state.
        if (setLayerType(slot, type, &tree) && onLayerPresetCommitted)
            onLayerPresetCommitted(slot);
        return;
    }
    if (slotType != type)
    {
        // A type changes only through the chooser or the header menu, never as a side effect of a preset.
        const auto* slotInfo = layerTypes.find(slotType);
        if (onLoadError)
        {
            // "a Kick", "an Arp": the article follows the type name's first letter.
            auto withArticle = [](const juce::String& name) { return (name.indexOfAnyOf("AEIOUaeiou") == 0 ? "an " : "a ") + name; };
            onLoadError("This is " + withArticle(fileInfo->name) + " preset; this slot is "
                        + withArticle(slotInfo != nullptr ? juce::String(slotInfo->name) : juce::String("layer")));
        }
        return;
    }
    stageLayerPreset(slot, tree, false, false);
}

void ProcessorBase::applyStagedTypedLayer(int slot, const juce::ValueTree& tree)
{
    const bool sameType = layerPresetType(tree) == slotTypeId(slot);
    jassert(sameType);   // a type change cancels the slot's stage, so a stage of another type cannot reach here
    if (! sameType) return;
    applyLayerNode(slot, tree);
    if (onLayerPresetCommitted) onLayerPresetCommitted(slot);
}

juce::File ProcessorBase::getDefaultPresetFile() const
{
    return getPresetsDir().getChildFile("_default." + getFullPresetExtension());
}

void ProcessorBase::loadDefaultPreset()
{
    const auto f = getDefaultPresetFile();
    if (f.existsAsFile())
        loadPreset(f);
}

void ProcessorBase::loadPreset(const juce::File& file)
{
    const char* tag = getFullPresetTag();
    if (tag == nullptr || ! file.existsAsFile()) { noteFullPresetLoadFailed(); return; }
    juce::String error;
    auto state = mu_pp::readFullPreset(file, tag, apvts.state.getType(), error);
    if (! state.isValid())
    {
        noteFullPresetLoadFailed();
        if (onLoadError) onLoadError(error);
        return;
    }
    useLoadedFullPreset(std::move(state));
    publishPresetName(file.getFileNameWithoutExtension());   // mu-link mixer display
}

juce::StringArray ProcessorBase::loadCategoryList() const
{
    const char* tag = getFullPresetTag();
    return tag != nullptr ? mu_pp::readPresetCategories(getPresetsDir(), getFullPresetExtension(), tag)
                          : juce::StringArray();
}

// ─── Structural layer edits ──────────────────────────────────────────────────

namespace
{
    // Suspends audio processing for the life of one structural edit and restores the earlier state.
    // suspendProcessing waits out a running block in the wrappers that hold the callback lock
    // (VST3, VST2, AU, AAX, LV2, standalone) but not in CLAP, which reads the flag unsynchronised;
    // layersLock protects the pointer moves in every host. layerEditInFlight additionally tells a
    // product's audio thread to skip rendering until the parameter moves have finished, so no
    // block plays a moved layer with half-shifted parameters.
    // layerEditInFlight is a flag, not a counter: edits never nest (all run on the message thread,
    // one at a time), and a nested guard would lower the flag early. release() ends the guard
    // early for a product whose audio never reads the moved parameters (layerHoldsParamTruth).
    struct ScopedSuspend
    {
        explicit ScopedSuspend(ProcessorBase& p) : proc(p), wasSuspended(p.isSuspended())
        {
            jassert(! proc.layerEditInFlight.load(std::memory_order_relaxed));   // edits do not nest
            proc.layerEditInFlight.store(true, std::memory_order_release);
            proc.suspendProcessing(true);
        }
        ~ScopedSuspend() { release(); }
        ScopedSuspend(const ScopedSuspend&)            = delete;
        ScopedSuspend& operator=(const ScopedSuspend&) = delete;

        // Resumes processing and lowers the flag; a second call does nothing.
        void release()
        {
            if (released) return;
            released = true;
            proc.suspendProcessing(wasSuspended);
            proc.layerEditInFlight.store(false, std::memory_order_release);
        }

        ProcessorBase& proc;
        const bool     wasSuspended;
        bool           released = false;
    };

    // Calls fn(param, idSuffix) for every ranged parameter whose id starts with `prefix`.
    template <class Fn>
    void forEachPrefixed(juce::AudioProcessor& proc, const juce::String& prefix, Fn&& fn)
    {
        for (auto* p : proc.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
            {
                const juce::String id = rp->getParameterID();
                if (id.startsWith(prefix)) fn(*rp, id.substring(prefix.length()));
            }
    }

    // The two id prefixes a layer slot's parameters sit under: the product's own (v3_) and the mixer strip's (ch3_).
    juce::StringArray layerParamPrefixes(const mu_pp::LayerLayout& layout, int slot)
    {
        juce::StringArray out;
        if (const auto own = layout.prefix(slot); own.isNotEmpty()) out.add(own);
        out.add("ch" + juce::String(slot) + "_");
        return out;
    }
}

void ProcessorBase::copyLayerParams(int from, int to)
{
    const auto src = layerParamPrefixes(layerLayout, from), dst = layerParamPrefixes(layerLayout, to);
    for (int k = 0; k < src.size(); ++k)
        forEachPrefixed(*this, src[k], [&](juce::RangedAudioParameter& rp, const juce::String& suffix)
        {
            if (auto* dp = apvts.getParameter(dst[k] + suffix)) dp->setValueNotifyingHost(rp.getValue());
        });
}

void ProcessorBase::swapLayerParams(int a, int b)
{
    const auto pa = layerParamPrefixes(layerLayout, a), pb = layerParamPrefixes(layerLayout, b);
    for (int k = 0; k < pa.size(); ++k)
        forEachPrefixed(*this, pa[k], [&](juce::RangedAudioParameter& rp, const juce::String& suffix)
        {
            if (auto* bp = apvts.getParameter(pb[k] + suffix))
            {
                const float va = rp.getValue(), vb = bp->getValue();
                rp.setValueNotifyingHost(vb);
                bp->setValueNotifyingHost(va);
            }
        });
}

void ProcessorBase::resetLayerParams(int slot)
{
    for (const auto& prefix : layerParamPrefixes(layerLayout, slot))
        forEachPrefixed(*this, prefix, [](juce::RangedAudioParameter& rp, const juce::String&)
        {
            rp.setValueNotifyingHost(rp.getDefaultValue());
        });
}

void ProcessorBase::remapSidechainSources(const std::function<int(int)>& remap)
{
    auto remapOne = [this, &remap](const juce::String& id)
    {
        if (auto* p = apvts.getParameter(id))
        {
            const int v  = juce::roundToInt(p->convertFrom0to1(p->getValue()));
            const int nv = juce::jlimit(0, mu_mixfx::kScSrcMax, remap(v));   // 0 off, 1..kMaxLayers a channel, then external, then the buses
            if (nv != v) p->setValueNotifyingHost(p->convertTo0to1((float) nv));
        }
    };
    for (int c = 0; c < mu_limits::kMaxLayers; ++c)
        remapOne("ch" + juce::String(c) + "_scSrc");
    for (const char* ret : { "ret_eff_scSrc", "ret_dly_scSrc", "ret_rev_scSrc" })
        remapOne(ret);
    for (int b = 0; b < mu_limits::kMaxBuses; ++b)
        remapOne("bus" + juce::String(b) + "_scSrc");
}

juce::ValueTree ProcessorBase::captureComposedState()
{
    auto state = mu_pp::captureState(apvts.state.getType(), layerLayout, layerExtras);

    // Each slot node gains its layer's clip bank and playing clip. Not part of captureLayer, so a layer
    // preset never carries a bank. An empty bank writes nothing, keeping a clip-free file unchanged.
    for (int i = 0; i < layerLayout.numLayers() && i < mu_limits::kMaxLayers; ++i)
        if (layers[(size_t) i] != nullptr)
            if (auto node = mu_pp::findLayerNode(state, i); node.isValid())
            {
                layers[(size_t) i]->clips->writeTo(node);
                if (layers[(size_t) i]->activeClip >= 0)
                    node.setProperty("activeClip", layers[(size_t) i]->activeClip, nullptr);
            }

    if (canEditLayers())
    {
        // The colour is a slot's identity, not part of its sound, so it lives on the full state, not in a layer node.
        juce::StringArray colours;
        for (int i = 0; i < mu_limits::kMaxLayers; ++i)
            colours.add(juce::String(layers[(size_t) i] != nullptr ? layers[(size_t) i]->colourIndex : i));
        state.setProperty("numVoices", numLayers.load(std::memory_order_relaxed), nullptr);
        state.setProperty("voiceColours", colours.joinIntoString(","), nullptr);
    }
    return state;
}

void ProcessorBase::applyComposedState(const juce::ValueTree& state)
{
    // A restore that lowers the count publishes the new count BEFORE the parameters move: applyState
    // writes defaults into the slots the state lacks, and those slots must already be out of the count
    // by then or they would render with default parameters until the store below. A restore that grows
    // the count keeps the count last, so no block sees a slot counted before its data is in.
    const bool editsLayers = canEditLayers();
    const int  newCount    = editsLayers ? savedLayerCount(state) : 0;
    bool       shrinkStored = false;
    if (editsLayers)
    {
        jassert(getLayerAccess() == mu_core::LayerAccess::TryLock);
        const juce::ScopedLock lw(layoutWriteLock);
        if (newCount < numLayers.load(std::memory_order_relaxed))
        {
            storeLayerCount(newCount);
            shrinkStored = true;
        }
    }
    if (shrinkStored && retireBin.countPolicy() == mu_core::RetireBin::CountPolicy::CutAtCount)
        retireBin.cutAtOrAbove(newCount);

    mu_pp::applyState(state, layerLayout, layerExtras);

    // Every slot takes its node's clips: a node without a <Clips> child (an older file, or a slot the
    // state lacks) empties the bank, so a smaller state leaves no stale clips past its layer count.
    for (int i = 0; i < layerLayout.numLayers() && i < mu_limits::kMaxLayers; ++i)
        if (layers[(size_t) i] != nullptr)
        {
            const auto node = mu_pp::findLayerNode(state, i);
            auto& layer = *layers[(size_t) i];
            layer.clips->readFrom(node);
            const int active = (int) node.getProperty("activeClip", -1);
            layer.activeClip = layer.clips->isOccupied(active) ? active : -1;
        }
    publishClipMasks();

    if (! editsLayers) return;

    // Count last on a grow, so the audio thread never sees a count ahead of the data; then the colours (a state without colours keeps the current ones).
    // Thread contract: hosts normally call setStateInformation on the message thread, and the editor's modulation
    // reads, the Stager and the products' lock-free paths all assume that. The layout store below is the one part
    // that is also safe from a host thread, so it goes through the packed layout word under layoutWriteLock: a lost
    // or misattached serial would break the retire bin's same-block hand-over. A product that parks a tail before this call holds layoutWriteLock
    // across both (it is re-entrant), so the park's nextLayoutSerial() is the serial this store carries.
    // A slot host keeps its count (the capacity): only the colours are restored.
    if (layersAreSlots())
    {
        const auto slotToks = juce::StringArray::fromTokens(state.getProperty("voiceColours", "").toString(), ",", "");
        for (int i = 0; i < mu_limits::kMaxLayers && i < slotToks.size(); ++i)
            if (layers[(size_t) i] != nullptr)
                layers[(size_t) i]->colourIndex = juce::jlimit(0, mu_limits::kMaxLayers - 1, slotToks[i].getIntValue());
        return;
    }

    // TryLock mode only until Published-mode quiesce exists: this store and the parameter moves above assume
    // the audio thread's try-lock protocol.
    int oldCount = 0;
    if (! shrinkStored)
    {
        const juce::ScopedLock lw(layoutWriteLock);
        oldCount = numLayers.load(std::memory_order_relaxed);
        storeLayerCount(newCount);
        // A regrow into slots whose spares were fading in place: count first (above), then cut, as addLayer does
        // (retire bin rule R1.3), so no block has neither the live layer nor the tail and a later shrink parks
        // afresh. Inside the same layout hold (order layoutWriteLock, binMutex), so no structural edit can move
        // another slot's tail into these rows between the store and the cut.
        if (regrowCutsInPlaceTails)
            for (int s = oldCount; s < newCount; ++s)
                retireBin.cutSlot(s);
    }
    if (! shrinkStored && retireBin.countPolicy() == mu_core::RetireBin::CountPolicy::CutAtCount)
        retireBin.cutAtOrAbove(newCount);
    const auto toks = juce::StringArray::fromTokens(state.getProperty("voiceColours", "").toString(), ",", "");
    for (int i = 0; i < mu_limits::kMaxLayers && i < toks.size(); ++i)
        if (layers[(size_t) i] != nullptr)
            layers[(size_t) i]->colourIndex = juce::jlimit(0, mu_limits::kMaxLayers - 1, toks[i].getIntValue());
}

int ProcessorBase::savedLayerCount(const juce::ValueTree& state) const
{
    return juce::jlimit(1, mu_limits::kMaxLayers, (int) state.getProperty("numVoices", unsavedLayerCount));
}

int ProcessorBase::firstUnusedColourIndex() const
{
    std::array<bool, mu_limits::kMaxLayers> used {};
    const int n = numLayers.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
        if (layers[(size_t) i] != nullptr)
        {
            const int c = layers[(size_t) i]->colourIndex;
            if (c >= 0 && c < mu_limits::kMaxLayers) used[(size_t) c] = true;
        }
    for (int c = 0; c < mu_limits::kMaxLayers; ++c)
        if (! used[(size_t) c]) return c;
    return 0;
}

int ProcessorBase::addLayer()
{
    const int n = numLayers.load(std::memory_order_relaxed);
    if (! canEditLayers() || n >= mu_limits::kMaxLayers) return -1;
    if (getLayerAccess() != mu_core::LayerAccess::TryLock) return -1;   // refused, never half done: a Published host uses setLayerType

    // Phase 1: build and prepare the new layer before anything is locked.
    auto fresh = createLayer(n);
    if (fresh == nullptr) return -1;
    cancelPendingSwap(n);                       // the new slot must carry no stale staged swap
    fresh->colourIndex = firstUnusedColourIndex();
    fresh->bindParams(apvts, n);                // the render contract: bound before it is published (no-op by default)

    // Declared before the suspend guard so a replaced spare is released after processing resumes.
    std::unique_ptr<Layer> retired;
    ScopedSuspend suspend(*this);

    // Phase 2: publish under layersLock (pointer move, count, a clean mixer strip), microseconds. The layout
    // writer lock comes first (lock order layoutWriteLock, layersLock, binMutex): the count store advances the serial.
    {
        const juce::ScopedLock lw(layoutWriteLock);
        const juce::ScopedLock sl(layersLock);
        retired = publishLayerLocked(std::move(fresh));
        // A regrow into a slot whose spare was fading in place: the count is published first, then the entry
        // is cut, so no block has neither the live layer nor the tail; and the spare `retired` is released only
        // after this cut (retire bin rules R1.3 and R1.4). Nothing is parked in any product today.
        retireBin.cutSlot(n);
        mixerEngine.channels[(size_t) n].reset();
        mixerEngine.resetSidechainEnv(n);
        layersMoved(n, n);
    }

    // A new layer starts with no clips. Explicit and outside layersLock: freeing trees is not a
    // microsecond job, and a re-used spare must not keep an old bank.
    clearLayerClips(n);

    // Phase 3: the slot's parameters back to defaults, still suspended so no block sees stale values.
    // Where the layer data is the truth the audio never reads these parameters, so processing resumes
    // first: the product's push runs beside live audio, as any edit of its own parameters does, and
    // layerAdded (which may read files) runs outside the edit's scope too.
    if (layerHoldsParamTruth())
    {
        suspend.release();
        syncSlotParams(n);
    }
    else
    {
        resetLayerParams(n);
    }
    layerAdded(n);
    return n;
}

bool ProcessorBase::removeLayer(int idx)
{
    const int n = numLayers.load(std::memory_order_relaxed);
    if (! canEditLayers() || n <= 1 || idx < 0 || idx >= n) return false;   // the last layer stays
    if (getLayerAccess() != mu_core::LayerAccess::TryLock) return false;   // the Published-mode quiesce is not built yet

    // Phase 1: the down-shift renumbers every layer from idx upward, so every staged per-layer swap
    // (keyed by index) would land on the wrong slot. A staged full preset replaces all slots and wins.
    for (int s = 0; s < mu_limits::kMaxLayers; ++s) cancelPendingSwap(s);

    // The slot the shift vacates stays filled when the product keeps spares; built before locking.
    std::unique_ptr<Layer> spare;
    if (keepSpareLayers)
    {
        spare = createLayer(n - 1);
        if (spare == nullptr) return false;
        spare->colourIndex = layers[(size_t) (n - 1)]->colourIndex;   // the spare keeps the slot's saved colour
    }

    std::unique_ptr<Layer> retired;             // released last, after processing resumes
    ScopedSuspend suspend(*this);

    // Phase 2: pointer shift, strip state shift and sidechain re-pointing under layersLock (after the layout
    // writer lock: the count store advances the serial).
    {
        const juce::ScopedLock lw(layoutWriteLock);
        const juce::ScopedLock sl(layersLock);
        mixerEngine.renumberSidechainsAfterRemove(idx);   // before the strips shift down
        retired = unpublishLayerLocked(idx, std::move(spare));
        if (retired != nullptr)
            layerRemoved(*retired, idx);   // before anything below moves the slots' state
        for (int d = idx; d < n - 1; ++d)
        {
            mixerEngine.channels[(size_t) d].copyFrom(mixerEngine.channels[(size_t) (d + 1)]);
            mixerEngine.moveSidechainEnv(d + 1, d);   // the follower moves with its strip
            layers[(size_t) d]->onMoved(d);
        }
        mixerEngine.channels[(size_t) (n - 1)].reset();
        mixerEngine.resetSidechainEnv(n - 1);
        // The removed slot's tails are cut; owned tails above it move down with their layers (empty bins in
        // every product today).
        retireBin.removeRow(idx, n);
        if (retireBin.countPolicy() == mu_core::RetireBin::CountPolicy::CutAtCount)
            retireBin.cutAtOrAbove(n - 1);
        layersReordered(LayerEdit::Remove, idx, -1);
        layersMoved(idx, n - 1);                // the shifted slots and the vacated top one
    }

    // The moved layers re-bind their parameter pointers to their new slot (C9) while layerEditInFlight still
    // keeps the audio thread from rendering them; outside layersLock because binding looks parameters up by id.
    for (int d = idx; d < n; ++d)
        if (layers[(size_t) d] != nullptr) layers[(size_t) d]->bindParams(apvts, d);

    // Phase 3: parameter values. Strips that ducked from the removed layer now duck from nothing and
    // the others follow their layer down (translated before the values shift).
    remapSidechainSources([idx](int v) { return mu_mix::sidechainParamAfterRemove(v, idx, mu_limits::kMaxLayers); });
    remapLayerReferences([idx](int i) { return mu_mix::sidechainAfterRemove(i, idx, mu_limits::kMaxLayers); });
    if (layerHoldsParamTruth())
    {
        suspend.release();                                 // the audio never reads these parameters: resume first
        for (int d = idx; d < n; ++d) syncSlotParams(d);   // the shifted slots and the vacated top one
    }
    else
    {
        for (int d = idx; d < n - 1; ++d) copyLayerParams(d + 1, d);
        resetLayerParams(n - 1);                // the vacated top slot
    }

    // The banks moved with their layers (the pointer shift), and so do the live playing clips; the vacated
    // top slot holds a spare or nothing and must start empty. Last, with processing resumed and the parameters
    // shifted, so the notification a surface acts on sees the finished edit. Remap first: the clear names the new index.
    suspend.release();
    liveState.remapLayers([idx](int i) { return mu_mix::sidechainAfterRemove(i, idx, mu_limits::kMaxLayers); });
    for (int d = idx; d < n - 1; ++d) presetLabels[(size_t) d] = presetLabels[(size_t) (d + 1)];   // labels follow their layer down
    clearLayerClips(n - 1);
    return true;
}

bool ProcessorBase::swapLayers(int a, int b)
{
    const int n = numLayers.load(std::memory_order_relaxed);
    if (! canEditLayers() || a < 0 || b < 0 || a >= n || b >= n || a == b) return false;
    if (getLayerAccess() != mu_core::LayerAccess::TryLock) return false;   // the Published-mode quiesce is not built yet

    // Phase 1: staged swaps are keyed by index, so swapping the two slots would misdirect them.
    cancelPendingSwap(a);
    cancelPendingSwap(b);

    ScopedSuspend suspend(*this);

    // Phase 2: pointers and strip state exchanged under layersLock; sidechain sources re-pointed first.
    {
        const juce::ScopedLock lw(layoutWriteLock);
        const juce::ScopedLock sl(layersLock);
        mixerEngine.renumberSidechainsAfterSwap(a, b);
        swapLayersLocked(a, b);
        storeLayerCount(numLayers.load(std::memory_order_relaxed), std::memory_order_relaxed);   // same count, next serial
        mixerEngine.swapChannelState(a, b);
        retireBin.swapRows(a, b);              // owned tails follow their slot, in-place ones are cut
        layers[(size_t) a]->onMoved(a);
        layers[(size_t) b]->onMoved(b);
        layersReordered(LayerEdit::Swap, a, b);
        layersMoved(a, a);                      // only the two exchanged slots, not those between
        layersMoved(b, b);
    }

    // Re-bind the two moved layers to their new slots (C9), still before any block can render them.
    layers[(size_t) a]->bindParams(apvts, a);
    layers[(size_t) b]->bindParams(apvts, b);

    // Phase 3: parameter values. A strip that ducked from one swapped layer now ducks from the other
    // (translated before the values swap, so each strip's own source moves with it).
    remapSidechainSources([a, b](int v) { return mu_mix::sidechainParamAfterSwap(v, a, b, mu_limits::kMaxLayers); });
    remapLayerReferences([a, b](int i) { return mu_mix::sidechainAfterSwap(i, a, b, mu_limits::kMaxLayers); });
    if (layerHoldsParamTruth())
    {
        suspend.release();                                 // the audio never reads these parameters: resume first
        syncSlotParams(a);
        syncSlotParams(b);
    }
    else
        swapLayerParams(a, b);

    // The banks and playing clips swapped with their layers. Last, after the parameters moved and processing
    // resumed, so the notification a surface acts on sees the finished edit.
    suspend.release();
    liveState.remapLayers([a, b](int i) { return mu_mix::sidechainAfterSwap(i, a, b, mu_limits::kMaxLayers); });
    std::swap(presetLabels[(size_t) a], presetLabels[(size_t) b]);
    publishClipMasks();
    return true;
}

void ProcessorBase::resetLayer(int idx)
{
    if (idx < 0 || idx >= numLayers.load(std::memory_order_relaxed) || layers[(size_t) idx] == nullptr) return;

    // A Published host never suspends: it gives the slot a fresh layer of the same type through two publications
    // (setLayerType, via Empty), then returns the strip and the name to defaults, as a reset always does.
    if (getLayerAccess() == mu_core::LayerAccess::Published)
    {
        const auto type = (mu_core::LayerTypeId) layers[(size_t) idx]->typeId();
        if (type != mu_core::LayerTypeId::Empty)
        {
            setLayerType(idx, mu_core::LayerTypeId::Empty);
            setLayerType(idx, type);
        }
        mixerEngine.requestSidechainEnvReset(idx);   // the strip returns to defaults, so its follower restarts too
        resetLayerParams(idx);
        layers[(size_t) idx]->name.clear();
        return;
    }
    cancelPendingSwap(idx);                     // a staged swap would re-fill what we are clearing

    // Phase 1: build the default data before anything is locked (constructing allocates).
    auto fresh = createLayer(idx);
    if (fresh == nullptr) return;               // a product without createLayer overrides resetLayer

    ScopedSuspend suspend(*this);
    {
        // Phase 2: take the default data in place under layersLock, so the editor's pointers into the
        // layer stay valid; `fresh` leaves holding the old data and is destroyed after unlocking.
        const juce::ScopedLock sl(layersLock);
        layers[(size_t) idx]->adoptDefaults(*fresh);
        mixerEngine.channels[(size_t) idx].reset();
        mixerEngine.resetSidechainEnv(idx);
        layersMoved(idx, idx);
    }

    // Reset means back to a freshly added layer, so the bank empties too (adoptDefaults leaves it alone).
    clearLayerClips(idx);

    if (layerHoldsParamTruth())
    {
        suspend.release();                                 // the audio never reads these parameters: resume first
        syncSlotParams(idx);
    }
    else
    {
        resetLayerParams(idx);
    }
}

// ─── Typed slots ─────────────────────────────────────────────────────────────────────────────

mu_core::LayerServices* ProcessorBase::ensureLayerServices(const mu_core::LayerTypeInfo& info)
{
    const int i = (int) info.id;
    if (info.createServices == nullptr || i < 0 || i >= mu_core::kLayerTypeIdCount) return nullptr;
    const juce::ScopedLock sl(layerServicesLock);
    if (layerServices[(size_t) i] != nullptr) return layerServices[(size_t) i].get();

    auto built = info.createServices(*this);
    if (built == nullptr) return nullptr;
    // A processor already prepared hands its rate and block size to services built after it; prepareLayerRender
    // reaches them through the table once they are in it, so a prepare racing this one still ends on the newest.
    double rate = 0.0;
    int block = 0;
    {
        const juce::ScopedLock ll(layersLock);
        rate  = layersPreparedRate;
        block = layersPreparedBlock;
    }
    if (rate > 0.0) built->prepare(rate, block);
    layerServices[(size_t) i] = std::move(built);
    return layerServices[(size_t) i].get();
}

mu_core::LayerServices* ProcessorBase::findLayerServices(mu_core::LayerTypeId id) const
{
    const int i = (int) id;
    if (i < 0 || i >= mu_core::kLayerTypeIdCount) return nullptr;
    const juce::ScopedLock sl(layerServicesLock);
    return layerServices[(size_t) i].get();
}
// docs/mu-tronic/design.md 2.1.2: a slot's type changes in two view publications with a grace wait between
// them, nothing suspended and nothing muted. The audio thread reaches layers only through the published view
// (LayerAccess::Published), so every pointer move here is invisible to it until a view is published.

mu_core::LayerView ProcessorBase::makeLayerView() const
{
    mu_core::LayerView v;
    v.numLive = numLayers.load(std::memory_order_relaxed);
    for (int s = 0; s < mu_limits::kMaxLayers; ++s) v.layer[(size_t) s] = layers[(size_t) s].get();
    return v;
}

void ProcessorBase::resetOwnLayerParams(int slot)
{
    if (const auto own = layerLayout.prefix(slot); own.isNotEmpty())
        forEachPrefixed(*this, own, [](juce::RangedAudioParameter& rp, const juce::String&)
        {
            rp.setValueNotifyingHost(rp.getDefaultValue());
        });
}

void ProcessorBase::publishLayers(int liveCount)
{
    jassert(getLayerAccess() == mu_core::LayerAccess::Published);
    const juce::ScopedLock lw(layoutWriteLock);
    const juce::ScopedLock sl(layersLock);
    auto v = makeLayerView();
    v.numLive = liveCount;
    v.serial = nextLayoutSerial();
    layerAccess.views.publish(v, layerAccess.epoch);   // the view first, then the word: the serial order of setLayerType
    storeLayerCount(liveCount);
}

bool ProcessorBase::setLayerType(int slot, mu_core::LayerTypeId type, const juce::ValueTree* initial)
{
    using mu_core::LayerTypeId;
    if (getLayerAccess() != mu_core::LayerAccess::Published) return false;   // a TryLock host edits through add / remove
    if (slot < 0 || slot >= numLayers.load(std::memory_order_relaxed) || layers[(size_t) slot] == nullptr) return false;
    const mu_core::LayerTypeInfo* info = type == LayerTypeId::Empty ? nullptr : layerTypes.find(type);
    if (type != LayerTypeId::Empty && info == nullptr) return false;
    if ((LayerTypeId) layers[(size_t) slot]->typeId() == type) return true;
    mu_hotswap::assertMessageThread();   // one caller at a time: the editor, a state restore and a swap commit are all message-thread work

    // Step 3: the UI lets go of the old layer (modulator panel, content) before it can leave the view.
    if (onLayerTypeChanging) onLayerTypeChanging(slot);

    // Step 1: a staged swap of the old type must not land on the new one.
    cancelPendingSwap(slot);

    // Step 2: build and prepare the new layer before anything is visible. A prepareToPlay on a host thread may
    // land before step 8; the generation read here is compared again there.
    uint32_t generation = 0;
    double rate = 0.0;
    int block = 0;
    {
        const juce::ScopedLock sl(layersLock);
        generation = prepareGeneration;
        rate  = layersPreparedRate;
        block = layersPreparedBlock;
    }
    // The type's shared services exist before its first layer does; the layer is wired to them before it is prepared.
    mu_core::LayerServices* services = info != nullptr ? ensureLayerServices(*info) : nullptr;
    std::unique_ptr<Layer> fresh = info != nullptr ? info->create(slot, type) : createLayer(slot);
    if (fresh == nullptr) return false;
    if (services != nullptr && info->attachServices != nullptr) info->attachServices(*fresh, *services, slot);
    if (rate > 0.0) fresh->prepare(rate, block);
    // The slot's identity stays with the slot: its colour and its name.
    fresh->colourIndex = layers[(size_t) slot]->colourIndex;
    fresh->name        = layers[(size_t) slot]->name;

    // Step 4: the last moment the old layer may read its parameters, none has changed yet.
    if (layers[(size_t) slot]->retirePolicy().kind != mu_core::RetirePolicy::Kind::None)
        layers[(size_t) slot]->beginRetire();

    // Step 5: publish view V1 with the slot empty and park the old layer in the bin. The bin entry is written
    // before V1 is published, with V1's serial as its activation, so the first block that loads V1 renders the
    // old layer as a tail and no block has neither. Lock order: layoutWriteLock, layersLock, binMutex.
    uint64_t afterV1 = 0;
    {
        const juce::ScopedLock lw(layoutWriteLock);
        const juce::ScopedLock sl(layersLock);
        std::unique_ptr<Layer> old = std::move(layers[(size_t) slot]);
        auto v1 = makeLayerView();
        v1.serial = nextLayoutSerial();
        retireBin.park(slot, std::move(old), v1.serial, false);
        layerAccess.views.publish(v1, layerAccess.epoch);
        storeLayerCount(numLayers.load(std::memory_order_relaxed));   // the layout word follows the view: the same serial
        afterV1 = layerAccess.epoch.stamp();
    }

    // Step 6: no block that still renders the old layer live can overlap step 7. A timeout costs at most one
    // block of a parameter mix; nothing is freed by step 7.
    layerAccess.epoch.waitFor(afterV1, 50);

    // Step 7: the new type's parameters to defaults (or the initial node) while nothing live reads them, then the
    // layer binds them. Only the NEW type's prefix is touched (a same-prefix type, Kick to Bass, shares t{N}_; the
    // old type's dormant rows are left alone and never saved). The mixer strip (ch{N}_) is not touched, and neither
    // is its sidechain follower: the strip and the source it ducks from are the same after a type change, so zeroing
    // the follower would only unduck a strip that is still being ducked. The new layer sits in layers[] but is not in
    // any view, so the audio thread cannot reach it.
    {
        const juce::ScopedLock sl(layersLock);
        layers[(size_t) slot] = std::move(fresh);
    }
    layerLayout.setLivePrefix(slot, info != nullptr && info->paramPrefixLetter != 0
                                        ? juce::String::charToString((juce::juce_wchar) info->paramPrefixLetter) + juce::String(slot) + "_"
                                        : juce::String());
    resetOwnLayerParams(slot);
    if (initial != nullptr && initial->isValid()) applyLayerNode(slot, *initial);
    layers[(size_t) slot]->bindParams(apvts, slot);
    layers[(size_t) slot]->completeSetup(apvts, slot);   // the layer's own message-thread setup, once, outside every lock
    clearLayerClips(slot);   // a type change empties the clip bank (a clip carries its type)

    // Step 8: V2 with the new layer. A prepare that ran since step 2 (a new rate or block size) is repeated for the
    // new layer first, so its first block is prepared for that block's rate and size.
    {
        const juce::ScopedLock lw(layoutWriteLock);
        const juce::ScopedLock sl(layersLock);
        if (prepareGeneration != generation && layersPreparedRate > 0.0)
            layers[(size_t) slot]->prepare(layersPreparedRate, layersPreparedBlock);
        auto v2 = makeLayerView();
        v2.serial = nextLayoutSerial();
        layerAccess.views.publish(v2, layerAccess.epoch);
        storeLayerCount(numLayers.load(std::memory_order_relaxed));
    }

    // Step 9: the old view and the old layer are freed after their grace by the views' reclaim and the bin's
    // service, on this side; ask for a pass.
    if (retireBin.hasPendingWork()) requestDeferredWork();
    publishClipMasks();
    return true;
}

void ProcessorBase::clearLayerClips(int slot)
{
    if (slot < 0 || slot >= mu_limits::kMaxLayers || layers[(size_t) slot] == nullptr) return;
    layers[(size_t) slot]->clips->clearAll();
    layers[(size_t) slot]->activeClip = -1;
    presetLabels[(size_t) slot] = juce::String();   // a fresh or reset layer came from no preset
    liveState.clearLayerClips(slot);
    publishClipMasks();
}

void ProcessorBase::noteNonClipStage(int slot)
{
    cancelPendingSwap(slot);   // even if this stage is then aborted, an earlier queued clip must not land unlit
    if (slot >= 0 && slot < mu_limits::kMaxLayers && layers[(size_t) slot] != nullptr)
    {
        layers[(size_t) slot]->activeClip = -1;
        presetLabels[(size_t) slot] = juce::String();   // the product re-notes the new preset's label after staging it
    }
    liveState.clearLayerClips(slot);   // a plain layer-preset load also supersedes a queued clip
}

void ProcessorBase::notePendingPresetTree(int slot, const juce::ValueTree& staged)
{
    if (slot < 0 || slot >= mu_limits::kMaxLayers) return;
    pendingPresetTree[(size_t) slot] = hasPendingSwap(slot) ? staged : juce::ValueTree();
}

void ProcessorBase::publishClipMasks()
{
    for (int i = 0; i < mu_limits::kMaxLayers; ++i)
    {
        liveState.setClipMask(i, clipMask(i));
        const auto* layer = getLayer(i);
        for (int c = 0; c < mu_limits::kClipsPerLayer; ++c)
            liveState.setClipName(i, c, layer != nullptr ? layer->clips->getName(c) : juce::String());
    }
}

juce::String ProcessorBase::clipLabel(int slot) const
{
    if (slot >= 0 && slot < mu_limits::kMaxLayers && presetLabels[(size_t) slot].isNotEmpty())
        return presetLabels[(size_t) slot];
    return getLayerName(slot);
}

void ProcessorBase::noteLayerPresetLabel(int slot, const juce::String& label)
{
    if (slot >= 0 && slot < mu_limits::kMaxLayers)
        presetLabels[(size_t) slot] = label;
}

bool ProcessorBase::storeClip(int slot, int clip, const juce::String& name)
{
    mu_hotswap::assertMessageThread();   // the clip bank is message-thread only (a controller callback must marshal first)
    auto* layer = getLayer(slot);
    if (layer == nullptr || ! mu_core::ClipBank::isValidIndex(clip)) return false;

    // A preset still waiting for its boundary is what the layer is about to be, but the live layer does not
    // show it yet: store that preset, and let it land as chosen.
    juce::ValueTree tree;
    if (hasPendingSwap(slot) && pendingPresetTree[(size_t) slot].isValid())
        tree = pendingPresetTree[(size_t) slot];
    else if (hasPendingSwap(slot))
    {
        // A queued clip launch: the store is authoritative, so drop it and the lamps that followed it.
        cancelPendingSwap(slot);
        layer->activeClip = -1;
        liveState.clearLayerClips(slot);
    }
    if (! tree.isValid())
        tree = captureLayerPresetTree(slot);
    if (! tree.isValid()) return false;

    // Named after the preset the layer came from unless the caller names it; the layer then carries that name.
    const juce::String stored = name.isNotEmpty() ? name : clipLabel(slot);
    layer->clips->store(clip, tree, stored);   // store deep-copies, so the capture is not aliased
    noteLayerPresetLabel(slot, stored);
    publishClipMasks();
    return true;
}

bool ProcessorBase::clearClip(int slot, int clip)
{
    mu_hotswap::assertMessageThread();   // the clip bank is message-thread only (a controller callback must marshal first)
    auto* layer = getLayer(slot);
    if (layer == nullptr || ! layer->clips->clear(clip)) return false;

    // The cleared clip can no longer be the one this layer is remembered as playing; a launch of it still
    // waiting for its boundary is cancelled (it would land a clip that no longer exists).
    if (layer->activeClip == clip)
        layer->activeClip = -1;
    liveState.clipCleared(slot, clip);
    publishClipMasks();
    return true;
}

bool ProcessorBase::loadClipFromFile(int slot, int clip, const juce::File& layerPreset)
{
    mu_hotswap::assertMessageThread();   // the clip bank is message-thread only (a controller callback must marshal first)
    auto* layer = getLayer(slot);
    if (layer == nullptr || ! mu_core::ClipBank::isValidIndex(clip)) return false;

    // The file must be this product's layer preset: its root tag is the tag a capture would write.
    const auto expectedTag = captureLayerPresetTree(slot).getType();
    std::unique_ptr<juce::XmlElement> xml;
    if (layerPreset.existsAsFile())
        xml = juce::XmlDocument::parse(layerPreset);
    if (xml == nullptr || expectedTag.toString().isEmpty() || ! xml->hasTagName(expectedTag.toString()))
    {
        if (onLoadError) onLoadError("Could not read \"" + layerPreset.getFileName() + "\"");
        return false;
    }

    // Stored as read: any migration runs at launch, on a copy.
    layer->clips->store(clip, juce::ValueTree::fromXml(*xml), layerPreset.getFileNameWithoutExtension());
    publishClipMasks();
    return true;
}

bool ProcessorBase::launchClip(int slot, int clip)
{
    mu_hotswap::assertMessageThread();   // the clip bank is message-thread only (a controller callback must marshal first)
    auto* layer = getLayer(slot);
    if (layer == nullptr || ! layer->clips->isOccupied(clip)) return false;
    if (hasPendingFullPreset()) return false;   // a staged full preset lands first and replaces the layers

    // The same stage path as a layer-preset load; get() deep-copies, so the stage can migrate the
    // copy and the bank is never written.
    // A hook that could not build the clip stages nothing and leaves the serial alone; a stage, and the
    // stopped path's apply-at-once (which cancels first), both move it.
    const auto serialBefore = slotStageSerial(slot);
    stageLayerPreset(slot, layer->clips->get(clip), true, true);
    const bool staged = slotStageSerial(slot) != serialBefore;
    if (staged)
        noteLayerPresetLabel(slot, layer->clips->getName(clip));   // the layer is now that clip: a re-store keeps its name
    return staged;
}

uint8_t ProcessorBase::clipMask(int slot) const
{
    const auto* layer = getLayer(slot);
    return layer != nullptr ? layer->clips->mask() : (uint8_t) 0;
}
