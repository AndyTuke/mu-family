#include "Plugin/PluginProcessor.h"

#include "License/LicenseKey.h"
#include "License/ProductLicensing.h"   // mu-core: ProcessorBase::initLicensing (licensed products only)


#include "Plugin/HostTransport.h"          // mu-core: DAW / mu-link transport read
#include "Plugin/TransportResolver.h"      // mu-core: the family transport rule

#include <cmath>

namespace mu_tilate
{

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Mixer channel strips, one per layer slot: the shared ch{N}_ rows the MixerChannel / MixerOverlay bind to.
    for (int i = 0; i < mu_limits::kMaxLayers; ++i)
        mu_mixfx::addChannelStripParams(layout, "ch" + juce::String(i) + "_", "Layer " + juce::String(i + 1) + " Ch ");

    // The Phrase type's rows for every layer slot (p{N}_*).
    for (int i = 0; i < mu_limits::kMaxLayers; ++i)
        addLayerParams(layout, i);

    // Shared global FX rack + returns + master. A Phrase has a loop of its own, so a staged swap lands on the master loop (0).
    mu_mixfx::addGlobalFxParams(layout, 0);
    return layout;
}

PluginProcessor::PluginProcessor()
    : ProcessorBase(BusesProperties()
                        .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)
                        .withOutput("Output",    juce::AudioChannelSet::stereo(), true),
                    createParameterLayout(),
                    juce::Identifier("MuTilateState")),
      mu_core::ProcessorLayerHostView(static_cast<ProcessorBase&>(*this)),
      PhraseSlotHost(static_cast<mu_core::ProcessorLayerHostView&>(*this))
{
    // All eight Phrases exist for the processor's lifetime (capacity is fixed, only the count is runtime).
    keepSpareLayers = true;
    for (int i = 0; i < mu_limits::kMaxLayers; ++i)
    {
        layers[(size_t) i] = createLayer(i);
        layers[(size_t) i]->colourIndex = i;   // each layer starts on its own palette colour
        jassert(dynamic_cast<Phrase*>(layers[(size_t) i].get()) != nullptr);
    }
    storeLayerCount(1, std::memory_order_relaxed);   // a fresh instance starts with one layer

    initAppSettings("muTilate");   // settings file + saved UI size / MIDI clock (ProcessorBase)

    // Offline licence + online activation (shared; after initAppSettings for getContentDir).
    initLicensing({ mu_tilate::kLicenseProductId, mu_tilate::kLicenseFilename, mu_tilate::kLicensePublicKey, "mutilate.activation", mu_tilate::kCheckoutUrl, mu_tilate::kSuggestedPrice });

    for (int i = 0; i < mu_limits::kMaxLayers; ++i) layers[(size_t) i]->bindParams(apvts, i);
    setupLayerState();
    layerSwapper.setAppliers([this](juce::ValueTree& t) { applyStateTree(t); },
                             [this](int i, juce::ValueTree& t) { applyLayerTree(i, t); });
    startFxParamSync();     // mixer / FX params -> mixerEngine + fxChain (ProcessorBase)
    loadStartupDefault();   // restore a saved _default preset (skipped by render mode)

   #if JUCE_DEBUG
    // Developer aid (Debug builds only): MU_TILATE_LOAD_LOOP names a loop to load into layer 1 at start, so the window can be
    // captured with a loop in it without driving the file chooser.
    if (const auto loop = juce::SystemStats::getEnvironmentVariable("MU_TILATE_LOAD_LOOP", {}); loop.isNotEmpty())
        loadLoopAsync(0, juce::File(loop));
   #endif
}

PluginProcessor::~PluginProcessor()
{
    prepareForDestruction();    // first: no deferred-work tick may reach this half-destroyed object
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    resetSwapPlayState();   // no processBlock is running yet: loads apply at once, not staged
    currentSampleRate.store(sampleRate, std::memory_order_relaxed);
    currentBlockSize.store(samplesPerBlock, std::memory_order_relaxed);
    mixerEngine.prepare(sampleRate, samplesPerBlock);
    fxChain.prepare(sampleRate, samplesPerBlock);
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
    queueMidiProgramChanges(midiMessages);
    const auto transport = resolveBlockTransport(midiMessages, buffer.getNumSamples());
    renderAndMix(buffer, midiMessages, transport);
    flagLayerSwapBoundaries(transport);
}

std::unique_ptr<Layer> PluginProcessor::createLayer(int slot)
{
    auto p = std::make_unique<Phrase>();
    p->insert->setSlotIndex(slot);   // per-slot noise seed: layers do not share one sequence
    p->prepare(currentSampleRate.load(std::memory_order_relaxed), currentBlockSize.load(std::memory_order_relaxed));
    return p;
}

// Slots whose Phrase changed lose the previous tenant's voice and insert memory. Runs under layersLock, which the audio
// thread holds for the whole of every block it renders, so the resets are safe and allocation-free.
void PluginProcessor::layersMoved(int firstIndex, int lastIndex)
{
    jassert(getLayerAccess() == mu_core::LayerAccess::TryLock);
    for (int d = juce::jmax(0, firstIndex); d <= juce::jmin(lastIndex, mu_limits::kMaxLayers - 1); ++d)
    {
        auto& ph = getPhrase(d);
        ph.playback->hardReset();
        ph.insert->setSlotIndex(d);
        ph.insert->reset();
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
            applyStateTree(toLayerState(tree));   // host restore: always immediate
            restoreLiveState(tree);
        }
    notifyHostStateRestored();
}

} // namespace mu_tilate

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mu_tilate::PluginProcessor();
}
