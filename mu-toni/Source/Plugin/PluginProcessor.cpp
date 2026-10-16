#include "Plugin/PluginProcessor.h"
#include "Plugin/TransportResolver.h"   // mu-core: the family transport rule
#include "License/ProductLicensing.h"   // mu-core: ProcessorBase::initLicensing (licensed products only)
#include "License/LicenseKey.h"
#include "Plugin/HostTransport.h"          // mu-core: DAW / mu-link transport read
#include "Modulation/ModulatorSerialise.h" // mu-core: modulator state save/load

namespace mu_toni
{

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // ── Mixer channel strips (one per layer slot, all eight) — shared `ch{N}_`
    //    binding the MixerChannel / MixerOverlay use: level/pan/mute/solo + FX
    //    sends + sidechain + output bus. Synced via ProcessorBase::syncGlobalFxParam.
    for (int i = 0; i < mu_limits::kMaxLayers; ++i)
    {
        const juce::String c = "ch" + juce::String(i) + "_";
        const juce::String n = "Layer " + juce::String(i + 1) + " Ch ";
        mu_mixfx::addChannelStripParams(layout, c, n);
    }

    // ── The Arp type's rows for every layer slot (a{N}_*), then the MIDI-in channel ──
    for (int i = 0; i < mu_limits::kMaxLayers; ++i)
        addLayerParams(layout, i);
    addArpGlobalParams(layout);

    // ── Shared global FX rack + returns + master (mu-core) ────────────────────
    mu_mixfx::addGlobalFxParams(layout, 2);   // no loop of its own: swaps land on the bar

    return layout;
}

PluginProcessor::PluginProcessor()
    : ProcessorBase(BusesProperties()
                        .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)
                        .withOutput("Output",    juce::AudioChannelSet::stereo(), true),
                    createParameterLayout(),
                    juce::Identifier("MuToniState"))
{
    // All eight Arps exist for the processor's lifetime (capacity is fixed, only the count is
    // runtime), so a preset can fill a slot above the live count before the count grows.
    keepSpareLayers = true;
    for (int i = 0; i < mu_limits::kMaxLayers; ++i)
    {
        layers[(size_t) i] = createLayer(i);
        layers[(size_t) i]->colourIndex = i;   // each layer starts on its own palette colour
        jassert(dynamic_cast<Arp*>(layers[(size_t) i].get()) != nullptr);   // the one type check; layerAs only casts
    }
    storeLayerCount(1, std::memory_order_relaxed);   // a fresh instance starts with one layer
    unsavedLayerCount = 4;                           // a state saved before layers were editable held four
    // A key pressed or released while an edit silences the audio still reaches the layers' held keys.
    setCarrySkippedMidi(true);

    // The processor's own chain (until the shared driver is the only path): the mixer's render hook runs
    // channel ch's Arp (engine -> insert -> mixer). Only called for the live layers, under the try-locked
    // layersLock.
    renderChannelCb = [this](int ch, juce::AudioBuffer<float>& buf, int n)
    {
        if (ch >= 0 && ch < mu_limits::kMaxLayers) getArp(ch).renderBlock(buf, n);
        else buf.clear();
    };

    // A saved MIDI map written under the old "v{N}_" ids is renamed as it loads (set before initAppSettings loads it).
    getMidiControlMap().setIdPrefixRenames(layerParamPrefixRenames(mu_limits::kMaxLayers));
    initAppSettings("muToni");   // settings file + saved UI size / MIDI clock (ProcessorBase)

    // Offline licence + online activation (shared; after initAppSettings for getContentDir).
    initLicensing({ mu_toni::kLicenseProductId, mu_toni::kLicenseFilename, mu_toni::kLicensePublicKey, "mutoni.activation", mu_toni::kCheckoutUrl });

    // The shared wavetable bank (procedural factory set), read by every voice's oscillators.
    bank.loadFactoryBank();
    noteInput.allocate((size_t) (kNoteInputCarry + kNoteInputBlock), true);

    for (int i = 0; i < mu_limits::kMaxLayers; ++i) layers[(size_t) i]->bindParams(apvts, i);   // every slot's rows
    setupLayerState();       // the layer layout every preset / session save and load uses
    layerSwapper.setAppliers([this](juce::ValueTree& t) { applyStateTree(t); },
                        [this](int i, juce::ValueTree& t) { applyLayerTree(i, t); });
    startFxParamSync();     // mixer / FX params → mixerEngine + fxChain (ProcessorBase)
    loadStartupDefault();   // restore a saved _default preset (skipped by render mode)
}

PluginProcessor::~PluginProcessor()
{
    prepareForDestruction();   // first: no deferred-work tick may reach this half-destroyed object
}

int PluginProcessor::decodeNoteInput(const juce::MidiBuffer& midi, int numSamples) noexcept
{
    // Short messages only, in time order (what juce::MidiBuffer iterates); a held-key scan reads nothing longer.
    int count = 0;
    for (const auto meta : midi)
    {
        if (meta.numBytes < 1 || meta.numBytes > 3) continue;
        if (count >= kNoteInputBlock) break;
        auto& e = noteInput[(size_t) (noteInputCarried + count++)];
        e.frame = juce::jlimit(0, juce::jmax(0, numSamples - 1), meta.samplePosition);
        e.size  = (uint8_t) meta.numBytes;
        for (int b = 0; b < 3; ++b) e.bytes[b] = b < meta.numBytes ? meta.data[b] : (uint8_t) 0;
    }
    return count;
}

void PluginProcessor::carryNoteInput(int count) noexcept
{
    // Channel messages only (a system message holds no key), moved down onto the carried prefix at frame 0.
    // The block's events start where the prefix ended; the prefix grows into them as they are kept.
    const int first = noteInputCarried;
    for (int i = 0; i < count && noteInputCarried < kNoteInputCarry; ++i)
    {
        const auto e = noteInput[(size_t) (first + i)];
        const auto type = (uint8_t) (e.bytes[0] & 0xf0);
        if (type < 0x80 || type == 0xf0) continue;
        auto& kept = noteInput[(size_t) noteInputCarried++];
        kept = e;
        kept.frame = 0;
    }
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    resetSwapPlayState();   // no processBlock is running yet: loads apply at once, not staged
    currentSampleRate.store(sampleRate, std::memory_order_relaxed);
    currentBlockSize.store(samplesPerBlock, std::memory_order_relaxed);
    mixerEngine.prepare(sampleRate, samplesPerBlock);
    fxChain.prepare(sampleRate, samplesPerBlock);

    // Every slot's Arp (runner and insert), spares included, and the shared driver's buffers.
    prepareLayerRender(sampleRate, samplesPerBlock);
    runnersLive = 0;   // the processor's own chain: every runner is reset as its slot goes live
}

void PluginProcessor::releaseResources()
{
    resetSwapPlayState();
    releaseLayerRender();
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // The shared block driver over Arp::render (the layer contract), behind the test switch until the
    // equivalence gate passes; the processor's own chain below is the default for this commit only.
    if (usesLayerRender())
    {
        queueMidiProgramChanges(midiMessages);
        const auto t = resolveBlockTransport(midiMessages, buffer.getNumSamples());
        renderAndMix(buffer, midiMessages, t);
        flagLayerSwapBoundaries(t);
        return;
    }

    const int numSamples = buffer.getNumSamples();

    // Preserve the DAW sidechain, then clear (shared — the SC input bus shares buffer
    // channels with the output, so a bare clear would wipe it).
    captureSidechainAndClear(buffer);

    // MIDI program change → preset load: queue matching PCs (Ch 1-8 layer, Ch 9 full) for
    // runDeferredWork to load on the message thread.
    queueMidiProgramChanges(midiMessages);

    // This block's transport — the family rule (mu-core TransportResolver): the host / mu-link
    // position, else (standalone) external MIDI clock, else the own transport. The arp steps and
    // the modulators both run on its beat.
    const double midiClockBeat = midiClockSync.process(midiMessages, numSamples, currentSampleRate.load(std::memory_order_relaxed));
    const auto   transport     = mu_core::resolveTransport(pollHostTransport(),
                                                           wrapperType == wrapperType_Standalone,
                                                           midiClockSync, midiClockBeat,
                                                           { internalPlaying, internalBpm, internalBeatPos, &nudgePhaseBeats, &nudgeDirection, &appliedSyncOffsetBeats },
                                                           numSamples, currentSampleRate.load(std::memory_order_relaxed), 0.0, getSyncOffsetMs());
    noteBlockTransport(transport);   // lets the next block's quantised control actions find the beat
    const double bpm       = transport.bpm;
    const bool   isPlaying = transport.playing;

    // Root-by-MIDI / trigger: this block's notes, decoded once for every layer's held keys.
    const int numNoteEvents = decodeNoteInput(midiMessages, numSamples);

    // Push parameters into each live layer's runner and render it through the shared path (engine
    // -> insert -> mixer). Guarded by a try-lock against a structural edit moving the Arps (the message
    // thread holds layersLock for those); on contention, or while an edit is still moving parameter
    // values, the block is left silent: a sub-ms gap during an edit.
    {
        const juce::ScopedTryLock layersTryLock(layersLock);
        if (layersTryLock.isLocked() && ! layerEditInFlight.load(std::memory_order_acquire))
        {
            const int numActive = numLayers.load(std::memory_order_relaxed);
            const int inCh = getArp(0).midiInChannel();   // the one MIDI-in row every Arp binds

            // Every slot's Arp hears the block's keys, spares included, then takes the block's transport.
            for (int i = 0; i < mu_limits::kMaxLayers; ++i)
            {
                auto& a = getArp(i);
                a.hearMidi(noteInput.get(), noteInputCarried + numNoteEvents, noteInputCarried, inCh);
                a.context.playing        = isPlaying;
                a.context.sampleRate     = currentSampleRate.load(std::memory_order_relaxed);
                a.context.bpm            = bpm;
                a.context.startBeat      = transport.startBeat;
                a.context.beatsPerSample = transport.beatsPerSample;
            }
            noteInputCarried = 0;

            // A slot that has just gone live starts from silence: a deleted layer's held note or
            // voice tail never resumes in the new one.
            for (int i = runnersLive; i < numActive; ++i) getArp(i).runner.reset();
            runnersLive = numActive;

            // Each live layer resolves its modulation at the block's beat and takes its parameters.
            for (int i = 0; i < numActive; ++i) getArp(i).readParams(transport.startBeat);

            // The render hook fills each channel; the mixer owns strip + master.
            processCoreBlock(buffer, nullptr, numActive, numSamples, bpm,
                             nullptr, nullptr, nullptr, &renderChannelCb);
        }
        else
            carryNoteInput(numNoteEvents);
    }

    // Hot-swap: flag staged presets whose bar line passed in this block (or the transport stopped).
    flagSwapBoundaries(isPlaying, transport.startBeat, transport.beatsPerSample * (double) numSamples);
}

// ── Dynamic layer management ─────────────────────────────────────────────────
// A fresh default Arp for slot `slot`. Built and prepared before a structural edit locks, so the
// edit under the lock only moves pointers (the insert and the voice allocate here).
std::unique_ptr<Layer> PluginProcessor::createLayer(int slot)
{
    auto a = std::make_unique<Arp>();
    a->runner.setBank(&bank);
    a->runner.setSlotIndex(slot, noiseSeedSalt);
    a->insert->setSlotIndex(slot);   // per-slot noise seed: layers do not share one sequence
    a->prepare(currentSampleRate.load(std::memory_order_relaxed), currentBlockSize.load(std::memory_order_relaxed));
    return a;
}

// Slots whose Arp changed lose the previous tenant's voice, step position and insert memory (the
// insert travels with its Arp, so it is re-seeded for the new slot and cleared). The mixer strip and
// its ducking envelope are reset by the edit itself. Allocation-free resets only.
//
// The held keys are not slot state: every Arp that has been in the processor hears every block, so all of
// them hold the same keys. An Arp built for this edit (an added layer, the spare a removal leaves on top)
// has heard nothing yet, so it takes slot 0's keys (slot 0 never holds such a new Arp). Safe here:
// layersMoved runs under layersLock, which the audio thread holds for the whole of every block it renders
// (TryLock mode).
void PluginProcessor::layersMoved(int firstIndex, int lastIndex)
{
    jassert(getLayerAccess() == mu_core::LayerAccess::TryLock);
    for (int d = juce::jmax(0, firstIndex); d <= juce::jmin(lastIndex, mu_limits::kMaxLayers - 1); ++d)
    {
        auto& arp = getArp(d);
        arp.runner.setSlotIndex(d, noiseSeedSalt);
        arp.runner.reset();
        arp.insert->setSlotIndex(d);
        arp.insert->reset();
        if (d != 0) arp.heldNotes = getArp(0).heldNotes;
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

} // namespace mu_toni

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mu_toni::PluginProcessor();
}
