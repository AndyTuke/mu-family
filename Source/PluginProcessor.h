#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Sequencer/SequencerEngine.h"
#include "Audio/VoiceEngine.h"

#include <array>

//==============================================================================
// PluginProcessor
// The main audio processor class. Inherits from juce::AudioProcessor.
// JUCE calls prepareToPlay, processBlock, and releaseResources.
// All plugin state is managed here via the APVTS.
//==============================================================================
class PluginProcessor : public juce::AudioProcessor
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    void toggleInternalPlay() { internalPlaying = !internalPlaying; if (!internalPlaying) internalBeatPos = 0.0; }
    bool isInternalPlaying()  const { return internalPlaying; }

    void    addRhythm    (const Rhythm& r) { sequencer.addRhythm(r); }
    void    removeRhythm (int index)       { sequencer.removeRhythm(index); }
    Rhythm& getRhythm    (int index)       { return sequencer.getRhythm(index); }
    int     getNumRhythms() const          { return sequencer.getNumRhythms(); }
    void    updatePattern (int index)      { sequencer.updatePattern(index); }

    void loadSampleForRhythm(int rhythmIndex, const juce::File& file);

    SequencerEngine sequencer;
    std::array<VoiceEngine, SequencerEngine::MaxRhythms> voiceEngines;

private:
    // Internal transport — used in standalone when no DAW playhead is available.
    bool   internalPlaying     = false;
    double internalBeatPos     = 0.0;
    double internalBpm         = 120.0;
    double currentSampleRate   = 44100.0;


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};