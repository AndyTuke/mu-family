#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include "SamplePlayer.h"

#include <array>

// Manages a pool of SamplePlayer voices sharing a single loaded sample.
// loadFile() is message-thread-only. trigger() and process() are audio-thread-only.
class VoiceEngine
{
public:
    static constexpr int MaxVoices = 4;

    VoiceEngine();

    void prepareToPlay(double sampleRate, int blockSize);
    void loadFile(const juce::File& file);
    void trigger();
    void process(juce::AudioBuffer<float>& output, int numSamples);
    bool hasSample() const;

private:
    juce::AudioFormatManager formatManager;

    juce::ReadWriteLock      bufferLock;
    juce::AudioBuffer<float> buffer;
    double originalSampleRate = 44100.0;
    double playbackRatio      = 1.0;   // originalSampleRate / currentSampleRate

    std::array<SamplePlayer, MaxVoices> voices;
    int    nextVoice         = 0;
    double currentSampleRate = 44100.0;
    bool   sampleLoaded      = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceEngine)
};
