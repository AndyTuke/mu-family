#include "Plugin/PluginProcessor.h"

#include "Audio/LoopLoader.h"              // mu-core: decode a loop file
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
                    juce::Identifier("MuTilateState"))
{
    // All eight Phrases exist for the processor's lifetime (capacity is fixed, only the count is runtime).
    keepSpareLayers = true;
    for (int i = 0; i < mu_limits::kMaxLayers; ++i)
    {
        layers[(size_t) i] = createLayer(i);
        layers[(size_t) i]->colourIndex = i;   // each layer starts on its own palette colour
        jassert(dynamic_cast<Phrase*>(layers[(size_t) i].get()) != nullptr);
        loopGeneration[(size_t) i].store(0);
    }
    storeLayerCount(1, std::memory_order_relaxed);   // a fresh instance starts with one layer

    initAppSettings("muTilate");   // settings file + saved UI size / MIDI clock (ProcessorBase)

    for (int i = 0; i < mu_limits::kMaxLayers; ++i) layers[(size_t) i]->bindParams(apvts, i);
    setupLayerState();
    layerSwapper.setAppliers([this](juce::ValueTree& t) { applyStateTree(t); },
                             [this](int i, juce::ValueTree& t) { applyLayerTree(i, t); });
    startFxParamSync();     // mixer / FX params -> mixerEngine + fxChain (ProcessorBase)
    loadStartupDefault();   // restore a saved _default preset (skipped by render mode)
}

PluginProcessor::~PluginProcessor()
{
    alive->store(false);        // a loop load still running on a worker must not reach this object
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

DetectSettings PluginProcessor::detectSettingsOf(int layer) const
{
    auto raw = [&](int row) { return apvts.getRawParameterValue(layerParamPrefix(layer) + ppi::suffix[row])->load(); };
    DetectSettings s;
    s.method      = (int) raw(ppi::dmethod);
    s.sensitivity = raw(ppi::dsens);
    s.minGapMs    = raw(ppi::dgap);
    s.preRollMs   = raw(ppi::dpre);
    s.lowCutHz    = raw(ppi::dlow);
    s.highCutHz   = raw(ppi::dhigh);
    return s;
}

// Decode on a worker, detect, lay the slices out in their original order, then publish on the message thread. A newer
// request for the layer (or a destroyed processor) makes an older completion do nothing.
void PluginProcessor::loadLoopAsync(int layer, const juce::File& file, bool keepSavedEdits)
{
    if (layer < 0 || layer >= mu_limits::kMaxLayers) return;
    const int gen = loopGeneration[(size_t) layer].fetch_add(1) + 1;
    loopStatus[(size_t) layer] = "Loading " + file.getFileName() + " ...";

    const auto detect = detectSettingsOf(layer);
    const double loopTempo = (double) apvts.getRawParameterValue(layerParamPrefix(layer) + ppi::suffix[ppi::ltempo])->load();
    auto flag = alive;

    juce::Thread::launch([this, layer, gen, file, detect, loopTempo, flag, keepSavedEdits]
    {
        auto loaded = std::make_shared<mu_core::LoadedLoop>(mu_core::loadLoop(file, mu_limits::kMaxLoopBytes));
        std::shared_ptr<SliceList> slices;
        if (loaded->ok())
        {
            const auto curve = analyseLoop(*loaded->audio, loaded->sampleRate, detect);
            slices = std::make_shared<SliceList>(detectSlices(curve, *loaded->audio, detect));
        }

        juce::MessageManager::callAsync([this, layer, gen, file, loaded, slices, loopTempo, flag, keepSavedEdits]
        {
            if (! flag->load() || loopGeneration[(size_t) layer].load() != gen) return;   // gone, or superseded
            if (! loaded->ok()) { loopStatus[(size_t) layer] = loaded->error; return; }

            auto& ph = getPhrase(layer);

            // A restore whose saved edits fit this loop keeps them (installLoop applies them); anything else gets the
            // detected slices laid out back to back in source order, as long as that takes (in bars of 4/4).
            const bool keep = keepSavedEdits && ph.savedEditsFit(loaded->audio->getNumSamples());
            ph.installLoop(loaded->audio, loaded->sampleRate, file.getFullPathName(), loaded->name);
            double total = 0.0;
            for (int i = 0; i < slices->size(); ++i)
                total += (double) slices->slice(i).length() / loaded->sampleRate * loopTempo / 60.0;
            if (! keep)
            {
                std::vector<double> beats;
                for (int i = 0; i < slices->size(); ++i)
                    beats.push_back((double) slices->slice(i).length() / loaded->sampleRate * loopTempo / 60.0);
                const int bars = juce::jlimit(1, 64, (int) std::ceil(total / 4.0 - 1.0e-6));
                Timeline timeline((double) bars * 4.0);
                timeline.originalOrder(beats);
                ph.commitEdit(*slices, timeline);
                if (auto* p = apvts.getParameter(layerParamPrefix(layer) + ppi::suffix[ppi::bars]))
                    p->setValueNotifyingHost(p->convertTo0to1((float) bars));
            }
            const auto shown = ph.getData();
            loopStatus[(size_t) layer] = loaded->name + ": " + juce::String(shown != nullptr ? shown->slices.size() : 0) + " slices, "
                                       + juce::String(total, 1) + " beats at " + juce::String(loopTempo, 0) + " BPM";
        });
    });
}

juce::String PluginProcessor::getLoopStatus(int layer) const
{
    if (layer < 0 || layer >= mu_limits::kMaxLayers) return {};
    const auto& s = loopStatus[(size_t) layer];
    return s.isNotEmpty() ? s : (getPhrase(layer).loopName().isNotEmpty() ? getPhrase(layer).loopName() : juce::String("No loop"));
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
