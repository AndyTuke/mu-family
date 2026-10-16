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

    // Per-channel render: each channel runs its own arpeggiator voice into the
    // buffer; the shared mixer applies the strip + master mix downstream. Only called for the
    // live layers, under the try-locked layersLock.
    renderChannelCb = [this](int ch, juce::AudioBuffer<float>& buf, int n)
    {
        if (ch >= 0 && ch < mu_limits::kMaxLayers)
        {
            getArp(ch).runner.render(buf, n, getArp(ch).context);
            getArp(ch).insert->process(buf, n, buf.getNumChannels(), getArp(ch).insertCfg);   // engine → insert → mixer
        }
        else buf.clear();
    };

    // A saved MIDI map written under the old "v{N}_" ids is renamed as it loads (set before initAppSettings loads it).
    getMidiControlMap().setIdPrefixRenames(layerParamPrefixRenames(mu_limits::kMaxLayers));
    initAppSettings("muToni");   // settings file + saved UI size / MIDI clock (ProcessorBase)

    // Offline licence + online activation (shared; after initAppSettings for getContentDir).
    initLicensing({ mu_toni::kLicenseProductId, mu_toni::kLicenseFilename, mu_toni::kLicensePublicKey, "mutoni.activation", mu_toni::kCheckoutUrl });

    // The shared wavetable bank (procedural factory set), read by every voice's oscillators.
    bank.loadFactoryBank();
    midiInChParam = apvts.getRawParameterValue(kMidiInChannelParamId);
    noteInput.allocate((size_t) (kNoteInputCarry + kNoteInputBlock), true);

    cacheVoiceParamPointers();
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

namespace
{
    // The per-voice slot (vpi::*) a modulation target drives, found from its table param name.
    int vpSlotFor(const char* param)
    {
        for (int k = 0; k < vpi::COUNT; ++k)
            if (std::strcmp(vpi::suffix[k], param) == 0) return k;
        jassertfalse;   // a table row names a parameter that does not exist
        return 0;
    }
}

void PluginProcessor::cacheVoiceParamPointers()
{
    for (int i = 0; i < mu_limits::kMaxLayers; ++i)
        for (int k = 0; k < kNumVoiceParams; ++k)
        {
            const juce::String id = layerParamId(i, vpi::suffix[k]);
            vp[(size_t) i][(size_t) k] = apvts.getRawParameterValue(id);
        }

    // Modulation-resolve inputs: ids + ranges (voice-independent) + per-voice atoms.
    for (int k = 0; k < kNumModDests; ++k)
    {
        modDestIds[(size_t) k]    = kModDestTable[k].id;
        modDestRanges[(size_t) k] = apvts.getParameterRange(layerParamId(0, kModDestTable[k].param));
        for (int i = 0; i < mu_limits::kMaxLayers; ++i)
            modDestAtoms[(size_t) i][(size_t) k] = vp[(size_t) i][(size_t) vpSlotFor(kModDestTable[k].param)];
        modParamValues[kModDestTable[k].id] = 0.0f;   // pre-size the map (no audio-thread alloc)
    }
}

void PluginProcessor::readVoice(int v, ArpParams& ap, ToniVoiceParams& tv,
                                int& rateIdx, float& gate01, bool& midiTrig, ArpAccent& accent)
{
    const auto& p = vp[(size_t) v];
    auto g = [&](int slot) { return p[(size_t) slot] != nullptr ? p[(size_t) slot]->load() : 0.0f; };

    // Resolve this voice's modulation matrix over its control sequences → out[] (param units).
    // Modulated destinations come from out[]; everything else reads the raw param.
    float out[kNumModDests];
    mu_mod::resolveLane(&getArp(v), modBeat, kNumModDests,
                        modDestIds.data(), modDestAtoms[(size_t) v].data(),
                        modDestRanges.data(), modParamValues, out);

    // Publish the post-matrix values for the knobs' live modulation arcs.
    for (int k = 0; k < kNumModDests; ++k)
        modLive[(size_t) v][(size_t) k].store(out[k], std::memory_order_relaxed);

    // Arp (integer dests round; direction/gate/porta continuous).
    ap.scale        = (int) g(vpi::scale);                  // not modulated
    ap.rootNote     = juce::roundToInt(out[D_root]);
    ap.rootOctave   = juce::roundToInt(out[D_roct]);
    ap.chord        = juce::roundToInt(out[D_chord]);
    ap.inversion    = juce::roundToInt(out[D_inv]);
    ap.octavesSpan  = juce::roundToInt(out[D_octs]);
    ap.direction    = out[D_dir];
    ap.diatonicSnap = g(vpi::snap) > 0.5f;                  // not modulated

    tv.osc1Table = (int) g(vpi::o1wt); tv.osc1Oct = (int) g(vpi::o1o); tv.osc1Fine = g(vpi::o1f); tv.osc1Pos = out[D_o1pos]; tv.osc1LevelDb = out[D_o1lvl];
    tv.osc2Table = (int) g(vpi::o2wt); tv.osc2Oct = (int) g(vpi::o2o); tv.osc2Semi = out[D_o2semi]; tv.osc2Fine = g(vpi::o2f);
    tv.osc2Pos   = out[D_o2pos]; tv.osc2LevelDb = out[D_o2lvl];
    tv.noiseLevelDb = out[D_noise]; tv.noiseType = (int) g(vpi::ntype);

    // Cross-mod (index / depth are 0..100 / ±100 knobs → 0..1 / ±1).
    tv.xmod.phaseMode = (int) g(vpi::xmPh);
    tv.xmod.index     = out[D_xmIdx] * 0.01f;
    tv.xmod.sync      = g(vpi::sync) > 0.5f;
    tv.xmod.feedback  = g(vpi::xmFb) > 0.5f;
    tv.xmod.ampMode   = (int) g(vpi::xmAm);
    tv.xmod.depth     = out[D_xmDep] * 0.01f;
    tv.xmod.ssbHz     = out[D_xmSsb];
    tv.filterType = (int) g(vpi::ft); tv.cutoff = out[D_cut]; tv.resonance = out[D_res]; tv.drive = out[D_drv]; tv.lowCutHz = g(vpi::locut);

    tv.ampA = g(vpi::aeA); tv.ampD = g(vpi::aeD); tv.ampS = g(vpi::aeS); tv.ampR = g(vpi::aeR); tv.ampLevelDb = out[D_amp];
    tv.fA = g(vpi::feA); tv.fD = g(vpi::feD); tv.fS = g(vpi::feS); tv.fR = g(vpi::feR); tv.filterEnvDepth = out[D_fenv];
    tv.pA = g(vpi::peA); tv.pD = g(vpi::peD); tv.pS = g(vpi::peS); tv.pR = g(vpi::peR); tv.pitchEnvDepth = out[D_penv];
    tv.pitchEnvTarget = (int) g(vpi::ptgt);

    tv.portamentoMs = out[D_porta];
    tv.legato       = g(vpi::leg) > 0.5f;
    tv.pan          = 0.0f;   // pan handled by the mixer strip

    rateIdx  = juce::roundToInt(out[D_rate]);
    gate01   = out[D_gate] * 0.01f;
    midiTrig = g(vpi::trig) > 0.5f;

    accent.amount  = out[D_acc] * 0.01f;
    accent.length  = (int) g(vpi::accLen);
    accent.pattern = (unsigned) juce::jmax(0, (int) g(vpi::accPat));
    accent.stepMask = (unsigned) juce::jmax(0, (int) g(vpi::stepMask));

    // Insert config (applied post-VCA in the render callback).
    auto& ic = getArp(v).insertCfg;
    ic.insertAlgo     = (int) g(vpi::drvChar);
    ic.insertParam[0] = g(vpi::insP1);
    ic.insertParam[1] = g(vpi::insP2);
    ic.insertParam[2] = g(vpi::insP3);
    ic.insertParam[3] = g(vpi::insP4);
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
    for (int i = 0; i < count && noteInputCarried < kNoteInputCarry; ++i)
    {
        const auto e = noteInput[(size_t) (noteInputCarried + i)];
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

    // Every slot's runner and insert, spares included; under the layers lock because a structural
    // edit moves the Arps.
    const juce::ScopedLock sl(layersLock);
    runnersLive = 0;   // every runner is reset as its slot goes live
    for (int i = 0; i < mu_limits::kMaxLayers; ++i)
    {
        getArp(i).runner.prepare(sampleRate, samplesPerBlock);
        getArp(i).insert->prepare(sampleRate, samplesPerBlock);
    }
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
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
    const int inCh = midiInChParam != nullptr ? juce::roundToInt(midiInChParam->load()) : 0;

    // Beat position for the modulation matrix (control-sequence playhead).
    modBeat = transport.startBeat;

    // Push parameters into each live layer's runner and render it through the shared path (engine
    // -> insert -> mixer). Guarded by a try-lock against a structural edit moving the Arps (the message
    // thread holds layersLock for those); on contention, or while an edit is still moving parameter
    // values, the block is left silent: a sub-ms gap during an edit.
    {
        const juce::ScopedTryLock layersTryLock(layersLock);
        if (layersTryLock.isLocked() && ! layerEditInFlight.load(std::memory_order_acquire))
        {
            const int numActive = numLayers.load(std::memory_order_relaxed);

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

            for (int i = 0; i < numActive; ++i)
            {
                ArpParams ap; ToniVoiceParams tv; int rateIdx = 6; float gate01 = 0.5f; bool midiTrig = false;
                ArpAccent accent;
                readVoice(i, ap, tv, rateIdx, gate01, midiTrig, accent);
                getArp(i).runner.setAccent(accent);
                getArp(i).runner.setArp(ap);
                getArp(i).runner.setVoiceParams(tv);
                getArp(i).runner.setStep(rateIdx, gate01, midiTrig);
            }

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
    const double sr = currentSampleRate.load(std::memory_order_relaxed);
    const int    bs = currentBlockSize.load(std::memory_order_relaxed);
    a->runner.prepare(sr, bs);
    a->insert->setSlotIndex(slot);   // per-slot noise seed: layers do not share one sequence
    a->insert->prepare(sr, bs);
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
