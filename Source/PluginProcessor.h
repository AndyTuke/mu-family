#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Sequencer/SequencerEngine.h"
#include "Audio/VoiceEngine.h"
#include "Audio/MidiOutputEngine.h"
#include "FX/FXChain.h"
#include "Audio/MixerEngine.h"

#include <array>
#include <unordered_map>

class PluginProcessor : public juce::AudioProcessor,
                        private juce::AudioProcessorValueTreeState::Listener
{
public:
    // apvts must be the first data member — initialized first, destroyed last.
    juce::AudioProcessorValueTreeState apvts;
    juce::StringArray                  loadedSamplePaths;  // [MaxRhythms]

    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    void   toggleInternalPlay()        { internalPlaying = !internalPlaying; if (!internalPlaying) internalBeatPos = 0.0; }
    bool   isInternalPlaying()   const { return internalPlaying; }
    void   setInternalBpm(double bpm)  { internalBpm = juce::jlimit(20.0, 300.0, bpm); }
    double getInternalBpm()      const { return internalBpm; }
    double getInternalBeatPos()  const { return internalBeatPos; }

    void    addRhythm    (const Rhythm& r);
    void    removeRhythm (int index)       { sequencer.removeRhythm(index); }
    Rhythm& getRhythm    (int index)       { return sequencer.getRhythm(index); }
    int     getNumRhythms() const          { return sequencer.getNumRhythms(); }
    void    updatePattern (int index)      { sequencer.updatePattern(index); }

    void loadSampleForRhythm(int rhythmIndex, const juce::File& file);

    juce::File getPresetsDir() const;
    void savePreset(const juce::String& name, const juce::String& description, const juce::String& category);
    void loadPreset(const juce::File& file);

    SequencerEngine sequencer;
    std::array<VoiceEngine,      SequencerEngine::MaxRhythms> voiceEngines;
    std::array<MidiOutputEngine, SequencerEngine::MaxRhythms> midiEngines;
    FXChain     fxChain;
    MixerEngine mixerEngine;

private:
    bool apvtsLoading = false;

    // Pre-allocated modulation parameter map — reused every block to avoid audio-thread allocation.
    // Keys match ModDest::ids.  Values are initialised in constructor and updated each block.
    std::unordered_map<std::string, float> modParamValues;

    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void syncRhythmParam(int ri, const juce::String& suffix, float v);
    void syncFXParam(const juce::String& id, float v);
    void syncMixerParam(const juce::String& id, float v);
    void pushRhythmToAPVTS(int ri);
    void restoreStateFromTree(const juce::ValueTree& state);

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    bool   internalPlaying   = false;
    double internalBeatPos   = 0.0;
    double internalBpm       = 120.0;
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
