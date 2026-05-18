#pragma once

#include <juce_dsp/juce_dsp.h>
#include "FX/FXAlgorithmDef.h"

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

    // Send-bus mode: when true, processInner outputs wet-only (no internal dry blend).
    // Used by EffectSlot since it's wired into the mixer's send/return path; the dry
    // signal is already in the main bus, so adding it back in via the algorithm's mix
    // would double the dry component. Default false (for any future insert use).
    void setSendMode(bool b) noexcept { sendMode = b; }
    bool isSendMode() const noexcept   { return sendMode; }

protected:
    bool sendMode = false;
};
