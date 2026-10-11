#include "Plugin/PluginProcessor.h"
#include "Plugin/ParameterLayout.h"
#include "Plugin/PluginEditor.h"
#include "Plugin/TransportResolver.h"            // mu-core: the family transport rule
#include "Persistence/SessionState.h"

#include <cmath>

namespace mu_clid_midi {

// A MIDI effect: no audio buses at all.
PluginProcessor::PluginProcessor()
    : ProcessorBase(BusesProperties(), createParameterLayout(), juce::Identifier("MuClidState")),
      core(apvts),
      rhythmPlayState(core.rhythmPlayState),
      beatFraction(core.beatFraction),
      sequencerPlaying(core.sequencerPlaying),
      lastBeatPos(core.lastBeatPos)
{
    initAppSettings("muClid");   // settings file + saved UI size / MIDI clock (ProcessorBase)

    // Automation can change a parameter on the audio thread; the pattern rebuild it needs runs here.
    startTimerHz(30);
}

PluginProcessor::~PluginProcessor()
{
    stopTimer();
}

//==============================================================================
// UTF-8 Greek lowercase mu (U+03BC) prefix, as everywhere else in the product's branding.
const juce::String PluginProcessor::getName() const
{
    return juce::String(juce::CharPointer_UTF8("\xce\xbc-Clid MIDI"));
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

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}

//==============================================================================
void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const auto transport = deriveTransport(numSamples);

    core.processBlock(transport.playing, transport.beatPos, midiMessages, numSamples);
}

void PluginProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    // Keep publishing the host transport (ProcessorCore), then let a pending note-off out.
    ProcessorBase::processBlockBypassed(buffer, midiMessages);
    core.flushPendingNoteOff(midiMessages, buffer.getNumSamples());
}

PluginProcessor::BlockTransport PluginProcessor::deriveTransport(int numSamples)
{
    // The family transport rule (mu-core TransportResolver): a plugin follows the host's play, tempo
    // and beat, and uses its own transport only when the host gives no position at all.
    const auto t = mu_core::resolveTransport(pollHostTransport(), false, midiClockSync, 0.0,
                                             { internalPlaying, internalBpm, internalBeatPos, &nudgePhaseBeats, &nudgeDirection, &appliedSyncOffsetBeats },
                                             juce::jmax(1, numSamples), currentSampleRate);
    noteBlockTransport(t);

    sequencerPlaying.store(t.playing);
    lastBeatPos.store(t.startBeat);
    return { t.playing, t.startBeat };
}

//==============================================================================
void PluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream(destData, true).writeString(session::toXmlString(apvts));
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    const auto xml = juce::String::fromUTF8((const char*) data, sizeInBytes);

    // The edit runs under suspendProcessing + the core's rhythm lock: suspend alone does not stop a
    // running block, the lock does (the audio thread's try-lock bails while it is held).
    suspendProcessing(true);
    juce::String error;
    const bool ok = core.restoreSession(xml, error);
    suspendProcessing(false);

    if (! ok && onLoadError)
        onLoadError(error);
}

} // namespace mu_clid_midi

// The JUCE plugin factory is the one entry point that stays at global scope.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mu_clid_midi::PluginProcessor();
}
