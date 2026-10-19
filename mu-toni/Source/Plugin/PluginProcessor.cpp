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
    // An Arp leaves at once (RetirePolicy None): the shared retire bin holds nothing, so the count policy
    // is the default and never decides anything here.

    // A saved MIDI map written under the old "v{N}_" ids is renamed as it loads (set before initAppSettings loads it).
    getMidiControlMap().setIdPrefixRenames(layerParamPrefixRenames(mu_limits::kMaxLayers));
    initAppSettings("muToni");   // settings file + saved UI size / MIDI clock (ProcessorBase)

    // Offline licence + online activation (shared; after initAppSettings for getContentDir).
    initLicensing({ mu_toni::kLicenseProductId, mu_toni::kLicenseFilename, mu_toni::kLicensePublicKey, "mutoni.activation", mu_toni::kCheckoutUrl });

    // The shared wavetable bank (procedural factory set), read by every voice's oscillators.
    bank.loadFactoryBank();

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

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    resetSwapPlayState();   // no processBlock is running yet: loads apply at once, not staged
    currentSampleRate.store(sampleRate, std::memory_order_relaxed);
    currentBlockSize.store(samplesPerBlock, std::memory_order_relaxed);
    mixerEngine.prepare(sampleRate, samplesPerBlock);
    fxChain.prepare(sampleRate, samplesPerBlock);

    // Every slot's Arp (runner and insert), spares included, and the shared driver's buffers. Each Arp
    // starts its next rendered block from silence.
    prepareLayerRender(sampleRate, samplesPerBlock);
}

void PluginProcessor::releaseResources()
{
    resetSwapPlayState();
    releaseLayerRender();
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // MIDI program change → preset load: queue matching PCs (Ch 1-8 layer, Ch 9 full) for
    // runDeferredWork to load on the message thread.
    queueMidiProgramChanges(midiMessages);

    // This block's transport — the family rule (mu-core TransportResolver): the host / mu-link
    // position, else (standalone) external MIDI clock, else the own transport. The arp steps and
    // the modulators both run on its beat.
    const auto transport = resolveBlockTransport(midiMessages, buffer.getNumSamples());

    // The shared block driver (docs/mu-tronic/design.md 2.1.1): the DAW sidechain and the clear per chunk,
    // the one try-locked access to the layers, every live Arp's render (held keys, modulation, arp, voice,
    // insert) into its slot's scratch, the parked spares following the keys, then the mixer and the FX. A
    // block during an edit or a lock miss renders silence, and its key messages reach the next block.
    renderAndMix(buffer, midiMessages, transport);

    // Hot-swap: flag staged presets whose bar line passed in this block (or the transport stopped). Each
    // slot's loop is its Arp's loopBeats (the 4/4 bar), as cached by the driver.
    flagLayerSwapBoundaries(transport);
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
            cancelAllStagedSwaps();               // nothing staged before the restore may land on top of it
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
