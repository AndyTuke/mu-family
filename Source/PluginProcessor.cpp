#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Sequencer/Rhythm.h"

//==============================================================================
PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties()
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    Rhythm defaultRhythm;
    defaultRhythm.name       = "Rhythm 1";
    defaultRhythm.genA.steps = 16;
    defaultRhythm.genA.hits  = 4;
    sequencer.addRhythm(defaultRhythm);
}

PluginProcessor::~PluginProcessor() {}

//==============================================================================
const juce::String PluginProcessor::getName() const { return "mu-Clid"; }
bool PluginProcessor::acceptsMidi() const { return false; }
bool PluginProcessor::producesMidi() const { return false; }
double PluginProcessor::getTailLengthSeconds() const { return 0.0; }

int PluginProcessor::getNumPrograms() { return 1; }
int PluginProcessor::getCurrentProgram() { return 0; }
void PluginProcessor::setCurrentProgram(int) {}
const juce::String PluginProcessor::getProgramName(int) { return {}; }
void PluginProcessor::changeProgramName(int, const juce::String&) {}

//==============================================================================
void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    for (auto& ve : voiceEngines)
        ve.prepareToPlay(sampleRate, samplesPerBlock);
}

void PluginProcessor::releaseResources() {}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                   juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midiMessages);
    buffer.clear();

    // Resolve beat position from DAW playhead, falling back to internal transport.
    double beatPos = 0.0;
    bool   playing = false;

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto ppq = pos->getPpqPosition())
                beatPos = *ppq;
        }
    }

    if (!playing && internalPlaying)
    {
        playing  = true;
        beatPos  = internalBeatPos;
        internalBeatPos += (buffer.getNumSamples() / currentSampleRate) * (internalBpm / 60.0);
    }

    if (playing)
    {
        const int firedMask = sequencer.processBlock(beatPos);
        for (int r = 0; r < sequencer.getNumRhythms(); ++r)
            if (firedMask & (1 << r))
                voiceEngines[r].trigger();
    }

    for (int r = 0; r < sequencer.getNumRhythms(); ++r)
        voiceEngines[r].process(buffer, buffer.getNumSamples());
}

//==============================================================================
bool PluginProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}

//==============================================================================
void PluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ignoreUnused(destData);
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::ignoreUnused(data, sizeInBytes);
}

//==============================================================================
void PluginProcessor::loadSampleForRhythm(int rhythmIndex, const juce::File& file)
{
    if (rhythmIndex < 0 || rhythmIndex >= SequencerEngine::MaxRhythms)
        return;
    voiceEngines[rhythmIndex].loadFile(file);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}