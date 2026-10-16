#include "Plugin/PluginProcessor.h"
#include "Plugin/TransportResolver.h"   // mu-core: the family transport rule
#include "License/ProductLicensing.h"   // mu-core: ProcessorBase::initLicensing (licensed products only)
#include "License/LicenseKey.h"
#include "Plugin/HostTransport.h"          // mu-core: DAW / mu-link transport read
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

    // Each track renders its engine into its own buffer first (GrooveVoices::renderAll, so a Rumble
    // finds its source already rendered); the hook hands the block to the shared mixer, which applies
    // the strip and the master mix.
    renderChannelCb = [this](int ch, juce::AudioBuffer<float>& buf, int n) { grooveVoices.copyOut(ch, buf, n); };

    seqSwingParam  = apvts.getRawParameterValue("seq_swing");
    seqAccentParam = apvts.getRawParameterValue("seq_accent");
    for (int i = 0; i < mu_limits::kMaxLayers; ++i) engineParam[(size_t) i] = apvts.getRawParameterValue(layerParamId(i, "eng"));
    grooveVoices.cacheParams(apvts);

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
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    resetSwapPlayState();   // no processBlock is running yet: loads apply at once, not staged
    currentSampleRate = sampleRate;
    currentBlockSize  = samplesPerBlock;
    mixerEngine.prepare(sampleRate, samplesPerBlock);
    fxChain.prepare(sampleRate, samplesPerBlock);

    // Every slot's engines, spares included; under the layers lock because the audio thread reads them.
    const juce::ScopedLock sl(layersLock);
    grooveVoices.prepare(sampleRate, samplesPerBlock);
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    // Preserve the DAW sidechain, then clear (shared — the SC input bus shares buffer
    // channels with the output, so a bare clear would wipe it).
    captureSidechainAndClear(buffer);

    // MIDI program change → preset load: queue matching PCs (Ch 1-8 track, Ch 9 full) for
    // runDeferredWork to load on the message thread.
    queueMidiProgramChanges(midiMessages);

    // This block's transport — the family rule (mu-core TransportResolver): the host / mu-link
    // position, else (standalone) external MIDI clock, else the own transport.
    const double midiClockBeat = midiClockSync.process(midiMessages, numSamples, currentSampleRate);
    const auto   transport     = mu_core::resolveTransport(pollHostTransport(),
                                                           wrapperType == wrapperType_Standalone,
                                                           midiClockSync, midiClockBeat,
                                                           { internalPlaying, internalBpm, internalBeatPos, &nudgePhaseBeats, &nudgeDirection, &appliedSyncOffsetBeats },
                                                           numSamples, currentSampleRate, 0.0, getSyncOffsetMs());
    noteBlockTransport(transport);   // lets the next block's quantised control actions find the beat
    const double bpm       = transport.bpm;
    const double beatStart = transport.startBeat;
    const bool   isPlaying = transport.playing;

    // Everything that touches the tracks or their engines runs under a try-lock against a structural
    // edit moving the Track pointers (the message thread holds layersLock for those); on contention, or
    // while an edit is still moving parameter values, the block is left silent: a sub-ms gap during an edit.
    {
        const juce::ScopedTryLock layersTryLock(layersLock);
        if (layersTryLock.isLocked() && ! layerEditInFlight.load(std::memory_order_acquire))
        {
            // This block's live tracks: the pointers are taken here and never kept past the block.
            const int numActive = numLayers.load(std::memory_order_relaxed);
            std::array<Track*, mu_limits::kMaxLayers> tracks {};
            for (int i = 0; i < numActive; ++i) tracks[(size_t) i] = &getTrack(i);
            grooveVoices.beginBlock(tracks.data(), numActive);

            // The global Swing / Accent seed every row's groove (the grid is still global).
            seedRowGrooves();

            // Refresh engine params from the APVTS, then clock the 909 sequencer for this block
            // (before the render so a step's engine is armed for this same block). Each fired track
            // triggers its engine and bumps a counter the editor polls to pulse the sidebar.
            grooveVoices.applyParams(beatStart, bpm);
            // Stop edge: silence the voices so a sustaining bass (no note-off yet) doesn't drone on.
            if (wasPlaying && ! isPlaying) grooveVoices.reset();
            wasPlaying = isPlaying;
            // Every track clocks its own row with its own swing and accent, at the block's beat (the same
            // arithmetic the grid clock had, so the rows fire as before). Spare tracks' clocks keep step too, so
            // a track that goes live fires on time; only the live rows trigger.
            if (isPlaying)
                for (int i = 0; i < mu_limits::kMaxLayers; ++i)
                {
                    auto& t = getTrack(i);
                    const bool live = i < numActive;
                    t.clock.processSpan(t.row, beatStart, numSamples, transport.beatsPerSample,
                                        t.row.swingAmount(), t.row.accentVelocity(),
                                        [this, &t, i, live](float vel, int off)
                                        {
                                            if (! live || engineOf(i) == EngineType::Rumble) return;   // a Rumble track does not step
                                            grooveVoices.trigger(i, vel, off);
                                            t.triggers.fetch_add(1, std::memory_order_relaxed);
                                        });
                }

            // Render the engines (Rumble last) → mixer through the shared path (engine→insert→mixer).
            grooveVoices.renderAll(numSamples);
            processCoreBlock(buffer, nullptr, numActive, numSamples, bpm,
                             nullptr, nullptr, nullptr, &renderChannelCb);
        }
    }

    // Hot-swap: flag staged presets whose pattern wrap (one bar) passed in this block, or the
    // transport stopped. When the beat comes from outside, the next block's start stands in for
    // this block's end, so the block's own span is estimated from the tempo.
    flagSwapBoundaries(isPlaying, beatStart, transport.beatsPerSample * (double) numSamples);
}

// ── Dynamic track management ─────────────────────────────────────────────────
// A fresh default Track for slot `slot`. Built before a structural edit locks, so the edit under the
// lock only moves pointers.
std::unique_ptr<Layer> PluginProcessor::createLayer(int slot)
{
    auto t = std::make_unique<Track>();
    t->owner.ptr = this;
    t->slot      = slot;
    seedRowGroove(t->row);
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

// Slots whose Track changed lose the previous tenant's voices and tails (the engines belong to the
// slot, not the Track), and the pattern is re-pointed at each slot's current Track row. The mixer strip
// and its ducking envelope are reset by the edit itself. Allocation-free resets only.
void PluginProcessor::layersMoved(int firstIndex, int lastIndex)
{
    for (int d = juce::jmax(0, firstIndex); d <= juce::jmin(lastIndex, mu_limits::kMaxLayers - 1); ++d)
    {
        grooveVoices.resetSlot(d);
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
