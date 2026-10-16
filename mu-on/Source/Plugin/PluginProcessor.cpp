#include "Plugin/PluginProcessor.h"
#include "License/ProductLicensing.h"   // mu-core: ProcessorBase::initLicensing (licensed products only)
#include "License/LicenseKey.h"
#include "Modulation/MuOnModDest.h"
#include "Modulation/ModulatorSerialise.h"   // mu-core: shared modulator (de)serialise

namespace mu_on
{

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    // ── Mixer channel strips — one per track slot (shared `ch{N}_` binding the MixerChannel /
    //    MixerOverlay use). Sidechains start unset; ducking is set up in the mixer.
    for (int i = 0; i < mu_limits::kMaxLayers; ++i)
        mu_mixfx::addChannelStripParams(layout, "ch" + String(i) + "_", "Track " + String(i + 1) + " Ch ");

    // ── Sequencer (global groove controls) ────────────────────────────────────
    addTrackGlobalParams(layout);

    // ── Per track: the engine, the Rumble source, then every engine's parameters (t{N}_*) ──────
    for (int i = 0; i < mu_limits::kMaxLayers; ++i)
        addLayerParams(layout, i);

    // ── Shared global FX rack + returns + master (mu-core) ────────────────────
    mu_mixfx::addGlobalFxParams(layout, 2);   // no loop of its own: swaps land on the bar

    return layout;
}

PluginProcessor::PluginProcessor()
    : ProcessorBase(BusesProperties()
                        .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)
                        .withOutput("Output",    juce::AudioChannelSet::stereo(), true),
                    createParameterLayout(),
                    juce::Identifier("MuOnState"))
{
    // All eight Tracks exist for the processor's lifetime (capacity is fixed, only the count is
    // runtime), so a preset can fill a slot above the live count before the count grows.
    keepSpareLayers = true;
    for (int i = 0; i < mu_limits::kMaxLayers; ++i)
    {
        layers[(size_t) i] = createLayer(i);
        layers[(size_t) i]->colourIndex = i;   // each track starts on its own palette colour
        jassert(dynamic_cast<Track*>(layers[(size_t) i].get()) != nullptr);   // the one type check; layerAs only casts
        stepPattern.bind(i, &getTrack(i).row);
    }
    storeLayerCount(1, std::memory_order_relaxed);         // a fresh instance is one Kick track...
    getTrack(0).row.loadDefaultGroove(EngineType::Kick);   // ...with the four-on-the-floor row
    unsavedLayerCount = kNumLegacyLanes;                   // a state saved before layers were variable held five lanes

    seqSwingParam  = apvts.getRawParameterValue("seq_swing");
    seqAccentParam = apvts.getRawParameterValue("seq_accent");
    for (int i = 0; i < mu_limits::kMaxLayers; ++i) engineParam[(size_t) i] = apvts.getRawParameterValue(layerParamId(i, "eng"));

    // Each track holds exactly one engine, the one its `t{N}_eng` parameter names: a change of that parameter
    // builds the new engine (message thread) and swaps it into the track.
    for (int i = 0; i < mu_limits::kMaxLayers; ++i)
    {
        engineListeners[(size_t) i] = std::make_unique<EngineParamListener>(*this, i);
        apvts.addParameterListener(layerParamId(i, "eng"), engineListeners[(size_t) i].get());
    }

    // A saved MIDI map written under the old "k_ b_ h_ s_ r_" ids is renamed as it loads (set before initAppSettings loads it).
    getMidiControlMap().setIdPrefixRenames(legacyLaneParamPrefixRenames());
    initAppSettings("muOn");   // settings file + saved UI size / MIDI clock (ProcessorBase)

    // Offline licence + online activation (shared; after initAppSettings for getContentDir).
    initLicensing({ mu_on::kLicenseProductId, mu_on::kLicenseFilename, mu_on::kLicensePublicKey, "muon.activation", mu_on::kCheckoutUrl });

    setupLayerState();      // the track layout every preset / session save and load uses
    layerSwapper.setAppliers([this](juce::ValueTree& t) { applyStateTree(t); },
                        [this](int i, juce::ValueTree& t) { applyTrackTree(i, t); });
    startFxParamSync();     // mixer / FX params → mixerEngine + fxChain (ProcessorBase)
    loadStartupDefault();   // restore a saved _default preset (skipped by render mode)
}

PluginProcessor::~PluginProcessor()
{
    prepareForDestruction();   // first: no deferred-work tick may reach this half-destroyed object
    for (int i = 0; i < mu_limits::kMaxLayers; ++i)
        apvts.removeParameterListener(layerParamId(i, "eng"), engineListeners[(size_t) i].get());
}

// A track's engine parameter changed. A mu-On track holds all five engines (it is never engineLocked), so it
// switches in the same block whatever thread the change came from, and syncEngine finds nothing to build. The
// path stays for a locked track (one engine, a mu-Tronic layer): on the message thread it builds the named engine
// now; from any other thread nothing may allocate, so the slot is flagged and the deferred work (message thread)
// builds it, the track rendering silence until then.
void PluginProcessor::engineParamChanged(int slot)
{
    if (juce::MessageManager::existsAndIsCurrentThread())
        getTrack(slot).syncEngine();
    else
    {
        enginesPending.fetch_or(1u << (unsigned) slot, std::memory_order_acq_rel);
        requestDeferredWork();
    }
}

void PluginProcessor::commitDeferredWork()
{
    const unsigned pending = enginesPending.exchange(0u, std::memory_order_acq_rel);
    for (int i = 0; i < mu_limits::kMaxLayers; ++i)
        if ((pending >> (unsigned) i) & 1u) getTrack(i).syncEngine();
}

void PluginProcessor::releaseResources()
{
    resetSwapPlayState();
    releaseLayerRender();
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    resetSwapPlayState();   // no processBlock is running yet: loads apply at once, not staged
    currentSampleRate = sampleRate;
    currentBlockSize  = samplesPerBlock;
    mixerEngine.prepare(sampleRate, samplesPerBlock);
    fxChain.prepare(sampleRate, samplesPerBlock);

    // Every slot's Track (its engine and buffers), spares included, and the shared driver's buffers.
    prepareLayerRender(sampleRate, samplesPerBlock);
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // MIDI program change → preset load: queue matching PCs (Ch 1-8 track, Ch 9 full) for runDeferredWork
    // to load on the message thread.
    queueMidiProgramChanges(midiMessages);

    // This block's transport by the family rule (the host / mu-link position, else external MIDI clock, else
    // the own transport), then the shared block driver: one try-lock of the layers, the tracks in priority order
    // (every non-Rumble track, then the Rumbles, which read their source's block), the mixer (strip, sidechain,
    // FX, master). A block the driver cannot render (an edit in flight, a missed lock) is silent.
    const auto t = resolveBlockTransport(midiMessages, buffer.getNumSamples());
    renderAndMix(buffer, midiMessages, t);

    // Hot-swap: flag the staged presets whose bar line (each track's loopBeats, 4) passed in this block, or the
    // transport stopped.
    flagLayerSwapBoundaries(t);
}

// ── Dynamic track management ─────────────────────────────────────────────────
// A fresh default Track for slot `slot`. Built before a structural edit locks, so the edit under the
// lock only moves pointers.
std::unique_ptr<Layer> PluginProcessor::createLayer(int slot)
{
    auto t = std::make_unique<Track>();
    t->slot      = slot;
    seedRowGroove(t->row);
    t->bindParams(apvts, slot);
    t->prepare(currentSampleRate, currentBlockSize);
    t->syncEngine();   // the engine the slot's parameter names, built and prepared now
    return t;
}

// The global groove into one row, and into every slot's row (spares included). Any thread: atomics only.
void PluginProcessor::seedRowGroove(StepRow& row) const noexcept
{
    row.setGroove(seqSwingParam  != nullptr ? seqSwingParam->load(std::memory_order_relaxed)  : 0.0f,
                  seqAccentParam != nullptr ? seqAccentParam->load(std::memory_order_relaxed) : 1.0f);
}

void PluginProcessor::seedRowGrooves() noexcept
{
    for (int i = 0; i < mu_limits::kMaxLayers; ++i) seedRowGroove(getTrack(i).row);
}

// A track that moved to another slot starts there from silence, its engine as freshly prepared (as each
// slot's first tenant did when the engines belonged to the slot), and the pattern view is re-pointed at each
// slot's current Track row. The mixer strip and its ducking envelope are reset by the edit itself.
// Under layersLock: allocation-free resets only.
void PluginProcessor::layersMoved(int firstIndex, int lastIndex)
{
    for (int d = juce::jmax(0, firstIndex); d <= juce::jmin(lastIndex, mu_limits::kMaxLayers - 1); ++d)
    {
        getTrack(d).resetEngineToPrepared();
        stepPattern.bind(d, &getTrack(d).row);
    }
}

int PluginProcessor::firstKickTrack() const noexcept
{
    const int n = numLayers.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
        if (engineOf(i) == EngineType::Kick) return i;
    for (int i = 0; i < n; ++i)
        if (engineOf(i) != EngineType::Rumble) return i;   // no Kick: any track a Rumble can process
    return 0;
}

// The new track takes the first engine (Kick, Bass, Hat, Snare, Rumble) that no live track runs, else
// Kick, and starts on that engine's groove; a Rumble starts on the first Kick.
void PluginProcessor::layerAdded(int index)
{
    std::array<bool, kNumEngineTypes> used {};
    for (int i = 0; i < index; ++i) used[(size_t) toIndex(engineOf(i))] = true;
    int pick = 0;
    for (int e = 0; e < kNumEngineTypes; ++e)
        if (! used[(size_t) e]) { pick = e; break; }
    const auto engine = engineFromIndex(pick);

    if (auto* p = apvts.getParameter(layerParamId(index, "eng")))
        p->setValueNotifyingHost(p->convertTo0to1((float) pick));
    if (engine == EngineType::Rumble)
        if (auto* p = apvts.getParameter(layerParamId(index, "src")))
            p->setValueNotifyingHost(p->convertTo0to1((float) firstKickTrack()));
    getTrack(index).row.loadDefaultGroove(engine);
}

void PluginProcessor::ensureRumbleSource(int track)
{
    if (track < 0 || track >= mu_limits::kMaxLayers || engineOf(track) != EngineType::Rumble) return;
    if (auto* p = apvts.getParameter(layerParamId(track, "src")))
    {
        const int src = juce::roundToInt(p->convertFrom0to1(p->getValue()));
        if (src == track || src >= numLayers.load(std::memory_order_relaxed) || engineOf(src) == EngineType::Rumble)
            p->setValueNotifyingHost(p->convertTo0to1((float) firstKickTrack()));
    }
}

void PluginProcessor::selectEngine(int i, EngineType engine)
{
    if (i < 0 || i >= mu_limits::kMaxLayers || engineOf(i) == engine) return;

    if (auto* p = apvts.getParameter(layerParamId(i, "eng")))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1((float) toIndex(engine)));
        p->endChangeGesture();
    }

    // The track's own Rumble source, and every Rumble that was processing this track (which now
    // plays another engine's audio or none): re-point to a usable source. Modulation assignments of
    // the old engine are left alone - inert while another engine runs, live again on switching back.
    const int live = numLayers.load(std::memory_order_relaxed);
    for (int j = 0; j < live; ++j) ensureRumbleSource(j);
}

// A Rumble track's source (`t{N}_src`) names a layer, so it follows that layer through a delete or a
// reorder; a deleted source becomes the first Kick. Runs in the suspended edit, before the parameter
// values shift, so each value is rewritten at its old slot and moves with it.
void PluginProcessor::remapLayerReferences(const std::function<int(int)>& oldToNew)
{
    // The first Kick in the new order (a source that is gone falls back to it).
    const int live = numLayers.load(std::memory_order_relaxed);
    int fallback = live;
    for (int j = 0; j < mu_limits::kMaxLayers; ++j)
        if (engineOf(j) == EngineType::Kick)
        {
            const int nj = oldToNew(j);
            if (nj >= 0 && nj < live) fallback = juce::jmin(fallback, nj);
        }
    if (fallback >= live) fallback = 0;

    for (int j = 0; j < mu_limits::kMaxLayers; ++j)
    {
        if (engineOf(j) != EngineType::Rumble) continue;
        auto* p = apvts.getParameter(layerParamId(j, "src"));
        if (p == nullptr) continue;
        const int oldSrc = juce::roundToInt(p->convertFrom0to1(p->getValue()));
        int newSrc = oldToNew(oldSrc);
        if (newSrc < 0) newSrc = fallback;
        if (newSrc != oldSrc) p->setValueNotifyingHost(p->convertTo0to1((float) newSrc));
    }
}

void PluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = captureState();
    saveLiveState(state);   // the host session also keeps the Performance (path, current item, mode)
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
        {
            const auto tree = juce::ValueTree::fromXml(*xml);
            applyStateTree(toLayerState(tree));   // host restore — always immediate
            restoreLiveState(tree);               // after the preset: it names the item that is playing
        }
    notifyHostStateRestored();
}


juce::File PluginProcessor::getLayerPresetDir() const { return getContentDir().getChildFile("Tracks"); }

} // namespace mu_on

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mu_on::PluginProcessor();
}
