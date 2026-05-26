#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

class TimeStretcherBase
{
public:
    virtual ~TimeStretcherBase() = default;

    virtual void prepare(double sampleRate, int blockSize) = 0;
    virtual void setTimeRatio(float ratio) = 0;
    virtual void setPitchRatio(float ratio) = 0;
    virtual void process(juce::AudioBuffer<float>&) = 0;
};
