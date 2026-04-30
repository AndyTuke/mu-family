#pragma once

#include <juce_dsp/juce_dsp.h>
#include "../FXAlgorithmDef.h"

// Abstract base for the 8 Effect slot algorithms.
// Implementations receive the oversampled rate (already multiplied) in prepareInner().
class EffectAlgorithmBase
{
public:
    virtual ~EffectAlgorithmBase() = default;

    virtual const FXAlgorithmDef& getDef() const = 0;

    // Called at the oversampled sample rate (sampleRate * oversamplingFactor).
    virtual void prepareInner(double sampleRate, int blockSize) = 0;

    // Process at the oversampled rate.
    virtual void processInner(juce::dsp::AudioBlock<float>&) = 0;

    virtual void setParam(const juce::String& id, float value) = 0;

    float getParam(const juce::String& id) const
    {
        for (auto& p : getDef().params)
            if (p.id == id)
                return p.defaultVal;
        return 0.0f;
    }
};
