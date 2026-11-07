#include "Plugin/PluginProcessor.h"
#include "Plugin/PluginEditor.h"
#include "Plugin/ParameterLayout.h"
#include "Plugin/TransportResolver.h"   // mu-core: the family transport rule

namespace mu_toni_midi
{

// A MIDI effect: no audio buses at all.
PluginProcessor::PluginProcessor()
    : ProcessorBase(BusesProperties(), createParameterLayout(), juce::Identifier("MuToniMidiState")),
      core(apvts)
{
    initAppSettings("muToniMidi");   // settings file + saved UI size / MIDI clock (ProcessorBase)
}

PluginProcessor::~PluginProcessor()
{
    prepareForDestruction();   // first: no deferred-work tick may reach this half-destroyed object
}

// UTF-8 Greek lowercase mu (U+03BC) prefix, as everywhere else in the product's branding.
const juce::String PluginProcessor::getName() const
{
    return juce::String(juce::CharPointer_UTF8("\xce\xbc-Toni MIDI"));
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    core.prepare(sampleRate, samplesPerBlock);
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet()  == juce::AudioChannelSet::disabled()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::disabled();
}

juce::AudioProcessorEditor* PluginProcessor::createEditor() { return new PluginEditor(*this); }

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const auto t = deriveTransport(numSamples);
    core.processBlock(t.playing, t.beatPos, t.bpm, midiMessages, numSamples);
}

void PluginProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    // Keep publishing the host transport (ProcessorCore), then let a sounding note end.
    ProcessorBase::processBlockBypassed(buffer, midiMessages);
    core.flushNoteOff(midiMessages);
}

PluginProcessor::BlockTransport PluginProcessor::deriveTransport(int numSamples)
{
    // The family transport rule (mu-core TransportResolver): a plugin follows the host's play, tempo and beat, and uses
    // its own transport only when the host gives no position at all.
    const auto t = mu_core::resolveTransport(pollHostTransport(), false, midiClockSync, 0.0,
                                             { internalPlaying, internalBpm, internalBeatPos, &nudgePhaseBeats, &nudgeDirection, &appliedSyncOffsetBeats },
                                             juce::jmax(1, numSamples), currentSampleRate);
    noteBlockTransport(t);
    return { t.playing, t.startBeat, t.bpm };
}

void PluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // The parameters, plus the modulators (control sequences and matrix) as a child node of their own.
    auto tree = apvts.copyState();
    for (auto stale = tree.getChildWithName(kModulatorNode); stale.isValid(); stale = tree.getChildWithName(kModulatorNode))
        tree.removeChild(stale, nullptr);   // a restored state keeps its node in the live tree: never write two
    juce::ValueTree extras(kModulatorNode);
    core.getLayer().writeExtras(extras);
    tree.appendChild(extras, nullptr);
    if (auto xml = tree.createXml())
        copyXmlToBinary(*xml, destData);
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // The edit runs under suspendProcessing so no block reads a half-restored state.
    suspendProcessing(true);
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
        {
            const auto tree = juce::ValueTree::fromXml(*xml);
            apvts.replaceState(tree);
            core.getLayer().applyExtras(tree.getChildWithName(kModulatorNode));   // an absent node clears the modulators
        }
    core.reset();
    suspendProcessing(false);
    notifyHostStateRestored();
}

} // namespace mu_toni_midi

// The JUCE plugin factory is the one entry point that stays at global scope.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mu_toni_midi::PluginProcessor();
}
