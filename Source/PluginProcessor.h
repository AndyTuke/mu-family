#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Sequencer/SequencerEngine.h"
#include "Audio/VoiceEngine.h"
#include "Audio/MidiOutputEngine.h"
#include "FX/FXChain.h"
#include "Audio/MixerEngine.h"
#include "License/LicenseChecker.h"

#include <memory>
#include <vector>
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
    void    removeRhythm (int index);
    Rhythm& getRhythm    (int index)       { return sequencer.getRhythm(index); }
    int     getNumRhythms() const          { return sequencer.getNumRhythms(); }
    void    updatePattern (int index)      { sequencer.updatePattern(index); }

    void loadSampleForRhythm(int rhythmIndex, const juce::File& file);

    juce::File getContentDir() const;
    juce::File getPresetsDir() const;
    juce::File getRhythmsDir() const;
    juce::File getSamplesDir() const;
    void setContentDir(const juce::File& dir);
    void ensureContentFoldersExist();

    // License — checked once at startup; result is immutable thereafter.
    LicenseChecker::Info licenseInfo;
    bool isLicensed() const { return kBetaBuild || licenseInfo.status == LicenseStatus::Licensed; }

    void savePreset(const juce::String& name, const juce::String& description,
                    const juce::String& category, bool embedSamples = false);
    void loadPreset(const juce::File& file);
    void saveRhythmPreset(int rhythmIndex, const juce::String& name, const juce::String& category);
    bool applyRhythmPreset(const juce::File& file, int rhythmIndex);
    bool applyDefaultRhythm(int rhythmIndex);
    void loadDefaultPreset();

    SequencerEngine sequencer;
    // Fixed-size arrays so the audio thread never races with a vector reallocation
    // caused by addRhythm/removeRhythm on the message thread.  numActiveRhythms is
    // the authoritative count; processBlock reads it atomically once per block.
    std::array<std::unique_ptr<VoiceEngine>, SequencerEngine::MaxRhythms> voiceEngines;
    std::array<MidiOutputEngine,             SequencerEngine::MaxRhythms> midiEngines;
    std::atomic<int> numActiveRhythms { 0 };
    FXChain     fxChain;
    MixerEngine mixerEngine;

    // Play-state atomics: written by audio thread, read by UI at 30 Hz.
    struct RhythmPlayState
    {
        juce::Atomic<int>  currentStep   { 0 };
        juce::Atomic<int>  patternLength { 1 };
        juce::Atomic<int>  stepsA        { 1 }; // individual ring step counts for per-ring rotation
        juce::Atomic<int>  stepsB        { 1 };
        juce::Atomic<int>  stepsC        { 1 };
        juce::Atomic<bool> hitFired      { false }; // legacy: kept for backward compat; new code uses hitCount
        juce::Atomic<int>  hitCount      { 0 };     // monotonic counter — audio thread increments per hit, UI tracks lastSeen (Issue #43: avoids one-shot-flag race between RhythmCircle + SidebarItem)
    };
    std::array<RhythmPlayState, SequencerEngine::MaxRhythms> rhythmPlayState;
    juce::Atomic<float>  beatFraction     { 0.0f }; // fractional position within the current 1/16 step
    juce::Atomic<bool>   sequencerPlaying { false };
    juce::Atomic<double> lastBeatPos      { 0.0 };  // most recent beat position (for UI playhead)

private:
    std::unique_ptr<juce::PropertiesFile> appSettings;

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
    int    currentBlockSize  = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
