#include "DelaySlot.h"
#include <cmath>

DelaySlot::DelaySlot()
{
    bufL.assign(MaxDelaySamples, 0.0f);
    bufR.assign(MaxDelaySamples, 0.0f);
}

void DelaySlot::prepare(double sampleRate, int blockSize)
{
    sr = sampleRate;
    bufL.assign(MaxDelaySamples, 0.0f);
    bufR.assign(MaxDelaySamples, 0.0f);
    writePosL = writePosR = 0;
    feedL = feedR = 0.0f;
    dryBuffer.setSize(2, blockSize);
    updateDelayFromMode();
}

void DelaySlot::process(juce::AudioBuffer<float>& buffer)
{
    if (!enabled) return;

    dryBuffer.makeCopyOf(buffer, true);

    const int numCh      = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    auto* outL = (numCh > 0) ? buffer.getWritePointer(0) : nullptr;
    auto* outR = (numCh > 1) ? buffer.getWritePointer(1) : outL;

    const int delL = juce::jlimit(1, MaxDelaySamples - 1, static_cast<int>(targetDelayL));
    const int delR = juce::jlimit(1, MaxDelaySamples - 1, static_cast<int>(targetDelayR));

    for (int i = 0; i < numSamples; ++i)
    {
        const float inL = (outL != nullptr) ? outL[i] : 0.0f;
        const float inR = (outR != nullptr) ? outR[i] : 0.0f;

        const int readPL = (writePosL - delL + MaxDelaySamples) % MaxDelaySamples;
        const int readPR = (writePosR - delR + MaxDelaySamples) % MaxDelaySamples;

        const float delayedL = bufL[readPL];
        const float delayedR = bufR[readPR];

        feedL = processDirt(delayedL * feedback);
        feedR = processDirt(delayedR * feedback);

        bufL[writePosL] = inL + feedL;
        bufR[writePosR] = inR + feedR;

        writePosL = (writePosL + 1) % MaxDelaySamples;
        writePosR = (writePosR + 1) % MaxDelaySamples;

        if (outL != nullptr) outL[i] = delayedL;
        if (outR != nullptr) outR[i] = delayedR;
    }

    // Insert-style blending (same curve as EffectSlot)
    float dryGain, wetGain;
    if (sendAmount <= 0.5f)
    {
        dryGain = 1.0f;
        wetGain = sendAmount * 2.0f;
    }
    else
    {
        dryGain = 1.0f - (sendAmount - 0.5f) * 2.0f;
        wetGain = 1.0f;
    }

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* wet = buffer.getWritePointer(ch);
        auto* dry = dryBuffer.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
            wet[i] = dry[i] * dryGain + wet[i] * wetGain;
    }
}

void DelaySlot::setDelayMs(float ms)
{
    delayMs = juce::jlimit(1.0f, 4000.0f, ms);
    if (timeMode == TimeMode::Free)
        updateDelayFromMode();
}

void DelaySlot::setTimeDivision(int denominator, bool dotted, bool triplet)
{
    syncDenominator = denominator;
    syncDotted      = dotted;
    syncTriplet     = triplet;
    if (timeMode == TimeMode::Sync)
        updateDelayFromMode();
}

void DelaySlot::updateDelayFromMode()
{
    float ms;
    if (timeMode == TimeMode::Free)
    {
        ms = delayMs;
    }
    else
    {
        // Beat duration in ms at current BPM
        const double beatMs = 60000.0 / hostBpm;
        // One beat = 1 quarter note, so a whole note = 4 beats
        double noteMs = beatMs * 4.0 / syncDenominator;
        if (syncDotted)   noteMs *= 1.5;
        if (syncTriplet)  noteMs *= 2.0 / 3.0;
        ms = static_cast<float>(noteMs);
    }

    const float sampL = static_cast<float>(ms * sr / 1000.0);
    const float sampR = sampL * (1.0f + spread * 0.1f);  // small R offset for stereo width
    setDelaySamplesLR(sampL, sampR);
}

void DelaySlot::setDelaySamplesLR(float sampL, float sampR)
{
    targetDelayL = juce::jlimit(1.0f, static_cast<float>(MaxDelaySamples - 1), sampL);
    targetDelayR = juce::jlimit(1.0f, static_cast<float>(MaxDelaySamples - 1), sampR);
}

float DelaySlot::processDirt(float x) const
{
    if (dirt < 0.001f) return x;
    // Soft saturation scaled by dirt amount
    const float gain  = 1.0f + dirt * 8.0f;
    const float sat   = std::tanh(x * gain) / gain;
    return x + (sat - x) * dirt;
}
