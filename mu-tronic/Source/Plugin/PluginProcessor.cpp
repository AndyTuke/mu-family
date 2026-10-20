#include "Plugin/PluginProcessor.h"
#include "Plugin/TransportResolver.h"   // mu-core: the family transport rule
#include "Persistence/TrackLayerTypes.h"  // the Rumble source remap

#include <set>

namespace mu_tronic
{

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // ── One mixer channel strip per slot (all sixteen) — the shared `ch{N}_` binding the MixerChannel and
    //    MixerOverlay use: level / pan / mute / solo, FX sends, sidechain. Synced by ProcessorBase::syncGlobalFxParam.
    for (int i = 0; i < mu_limits::kMaxLayers; ++i)
    {
        const juce::String c = "ch" + juce::String(i) + "_";
        const juce::String n = "Slot " + juce::String(i + 1) + " Ch ";
        mu_mixfx::addChannelStripParams(layout, c, n);
    }

    // ── The group buses (mu_limits::kMaxBuses, defined for this target): bus{B}_ strips and their inserts.
    mu_mixfx::addBusParams(layout);

    // ── Every registered type's own rows, for every slot. A type's table is added once per prefix (the five mu-On
    //    engines will share t{N}_), so each prefix exists on all sixteen slots and the type only selects the live one.
    mu_core::LayerTypeRegistry types;
    registerLayerTypes(types);
    std::set<char> declaredPrefixes;
    for (int t = 0; t < types.size(); ++t)
    {
        const auto& info = types.at(t);
        if (info.addLayerParams == nullptr || ! declaredPrefixes.insert(info.paramPrefixLetter).second) continue;
        for (int i = 0; i < mu_limits::kMaxLayers; ++i)
            info.addLayerParams(layout, i);
    }

    // ── Shared global FX rack + returns + master (mu-core) ────────────────────
    mu_mixfx::addGlobalFxParams(layout, 2);   // swaps land on the bar until a looping type is registered

    return layout;
}

PluginProcessor::PluginProcessor()
    : ProcessorBase(BusesProperties()
                        .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)
                        .withOutput("Output",    juce::AudioChannelSet::stereo(), true),
                    createParameterLayout(),
                    juce::Identifier(kStateType))
{
    // The audio thread reads layers only through the published view, so a type change mutes nothing.
    setLayerAccess(mu_core::LayerAccess::Published);
    keepSpareLayers = true;
    registerLayerTypes(layerTypes);

    // Every slot holds a layer for the processor's lifetime (Empty until the user chooses a type); the live
    // count is the capacity, so the first view lists all sixteen.
    for (int i = 0; i < mu_limits::kMaxLayers; ++i)
    {
        auto layer = createLayer(i);
        layer->colourIndex = i % mu_limits::kLayersPerPage;   // distinct within a page (the palette has as many colours as a page has slots)
        layer->bindParams(apvts, i);
        layers[(size_t) i] = std::move(layer);   // layer-ok: constructor, nothing renders before publishLayers
    }
    publishLayers(mu_limits::kMaxLayers);

    initAppSettings(kAppName);   // settings file + saved UI size / MIDI clock (ProcessorBase); no licence: freeware

    setupLayerState();       // the layer layout every preset / session save and load uses
    layerSwapper.setAppliers([this](juce::ValueTree& t) { applyStateTree(t); },
                             [this](int slot, juce::ValueTree& t) { applyStagedTypedLayer(slot, t); });   // a layer preset lands on its slot's wrap
    startFxParamSync();      // mixer / FX params → mixerEngine + fxChain (ProcessorBase)
    loadStartupDefault();    // restore a saved _default preset (skipped by render mode)
}

PluginProcessor::~PluginProcessor()
{
    prepareForDestruction();   // first: no deferred-work tick may reach this half-destroyed object
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    resetSwapPlayState();   // no processBlock is running yet: loads apply at once, not staged
    mixerEngine.prepare(sampleRate, samplesPerBlock);
    fxChain.prepare(sampleRate, samplesPerBlock);

    // Every slot's layer, and the shared driver's buffers.
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

    // MIDI program change → preset load: queue matching PCs for runDeferredWork to load on the message thread.
    queueMidiProgramChanges(midiMessages);

    // This block's transport — the family rule (mu-core TransportResolver): external MIDI clock, else the own transport.
    const auto transport = resolveBlockTransport(midiMessages, buffer.getNumSamples());

    // The shared block driver (docs/mu-tronic/design.md 2.1.1): the sidechain and the clear per chunk, the one
    // published view of the layers, every live layer's render into its slot's scratch, then the mixer and the FX.
    renderAndMix(buffer, midiMessages, transport);

    // Hot-swap: flag staged presets whose boundary passed in this block (each slot's loop is its layer's loopBeats).
    flagLayerSwapBoundaries(transport);
}

void PluginProcessor::remapLayerReferences(const std::function<int(int)>& oldToNew)
{
    remapRumbleSources(*this, oldToNew);
}

std::unique_ptr<Layer> PluginProcessor::createLayer(int slot)
{
    juce::ignoreUnused(slot);
    return std::make_unique<EmptyLayer>();
}

int PluginProcessor::getNumActiveLayers() const
{
    int n = 0;
    for (int i = 0; i < mu_limits::kMaxLayers; ++i)
        if (getLayerTypeId(i) != mu_core::LayerTypeId::Empty) ++n;
    return n;
}

mu_core::LayerTypeId PluginProcessor::getLayerTypeId(int idx) const
{
    const auto* layer = getLayer(idx);
    return layer != nullptr ? (mu_core::LayerTypeId) layer->typeId() : mu_core::LayerTypeId::Empty;
}

juce::String PluginProcessor::getLayerName(int idx) const
{
    const auto* layer = getLayer(idx);
    if (layer == nullptr) return {};
    if (! layer->name.empty()) return juce::String(layer->name);
    const auto type = getLayerTypeId(idx);
    // The default: "Slot 5" while Empty, "<Type> 5" once the slot holds a layer.
    return juce::String(type == mu_core::LayerTypeId::Empty ? "Slot" : mu_core::layerTypeName(type)) + " " + juce::String(idx + 1);
}

void PluginProcessor::setLayerName(int idx, const juce::String& name)
{
    auto* layer = getLayer(idx);
    if (layer == nullptr) return;
    const auto trimmed = name.trim();
    // Typing the default back (or nothing) returns the slot to deriving its name.
    layer->name.clear();
    if (trimmed.isNotEmpty() && trimmed != getLayerName(idx))
        layer->name = trimmed.toStdString();
}

// A type's layer presets are its product's own folder, shared with it and never copied (ProcessorBase finds it from
// the registry): <TDP>/muToni/Arps for Arp. The folder and extension answer for the selected slot's type.
juce::File PluginProcessor::getLayerPresetDir() const
{
    return typedLayerPresetDir(getSelectedSlot());
}

juce::String PluginProcessor::getLayerPresetExtension() const
{
    return typedLayerPresetExtension(getSelectedSlot());
}

juce::Array<juce::File> PluginProcessor::slotPresetFiles(int slot) const
{
    return typedSlotPresetFiles(slot);
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
            applyStateTree(toLayerState(tree));   // standalone restore — always immediate
            restoreLiveState(tree);               // after the preset: it names the item that is playing
        }
    notifyHostStateRestored();
}

} // namespace mu_tronic

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mu_tronic::PluginProcessor();
}
