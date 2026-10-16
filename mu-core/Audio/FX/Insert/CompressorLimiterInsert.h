#pragma once

#include "InsertAlgorithmBase.h"
#include <cmath>

// insertAlgo = 7 (Compressor) and 8 (Limiter). Same envelope-follower
// algorithm; the differences are the ratio (4:1 vs 100:1) and, for the Limiter, a
// brick-wall peak stage after it, both selected from p.insertAlgo at process time. Sharing one class avoids duplicating
// ~25 lines of identical code; storing one instance at both array indices 7
// and 8 of InsertProcessor's dispatch table is fine because the instance reads
// the ratio per-call from its parameters.
class CompressorLimiterInsert : public InsertAlgorithmBase
{
public:
    void prepare(double sampleRate, int) override
    {
        currentSampleRate = sampleRate;
        smoothedAtt   .reset(sampleRate, 0.015);  // 15 ms — eliminates att/rel knob crackle
        smoothedRel   .reset(sampleRate, 0.015);
        smoothedThresh.reset(sampleRate, 0.015);  // 15 ms — eliminates threshold knob crackle
        smoothedOut   .reset(sampleRate, 0.015);  // 15 ms — same pattern, applied to output gain
        smoothedAtt   .setCurrentAndTargetValue(1.0f);
        smoothedRel   .setCurrentAndTargetValue(1.0f);
        smoothedThresh.setCurrentAndTargetValue(1.0f);
        smoothedOut   .setCurrentAndTargetValue(1.0f);
        reset();
    }
    void reset() override
    {
        envelope[0] = envelope[1] = 0.0f;
        limGain[0] = limGain[1] = 1.0f;
        // Snap the smoothers to their prepare() values so a reset slot ramps in exactly as a fresh one.
        smoothedAtt   .setCurrentAndTargetValue(1.0f);
        smoothedRel   .setCurrentAndTargetValue(1.0f);
        smoothedThresh.setCurrentAndTargetValue(1.0f);
        smoothedOut   .setCurrentAndTargetValue(1.0f);
    }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& grOut) override
    {
        const float sr        = (float)currentSampleRate;
        // Slot 0 = Threshold 0..100 (mapped to 0..-40 dB), Slot 1 = Output -24..24 dB,
        // Slot 2 = Attack 0..100 ms, Slot 3 = Release 20..2000 ms (log).
        const float threshKnob = insertSlot(p, 0);
        const float outputDb   = insertSlot(p, 1);
        const float attackKnob = insertSlot(p, 2);
        const float relMs      = juce::jmax(1.0f, insertSlot(p, 3));
        const float threshLin  = juce::Decibels::decibelsToGain(-(threshKnob / 100.0f) * 40.0f);
        const float outGain    = juce::Decibels::decibelsToGain(outputDb);
        const float attackMs   = juce::jmax(0.1f, attackKnob);   // the knob is in ms, as labelled
        const bool  isLimiter  = (p.insertAlgo == 8);
        const float ratio      = isLimiter ? 100.0f : 4.0f;
        smoothedAtt   .setTargetValue(std::exp(-2.2f / (attackMs * 0.001f * sr)));
        smoothedRel   .setTargetValue(std::exp(-2.2f / (relMs    * 0.001f * sr)));
        smoothedThresh.setTargetValue(threshLin);
        smoothedOut   .setTargetValue(outGain);

        float peakGainDb = 0.0f;  // 0 or negative — fed back to UI meter via grOut
        // Sample-major: the four smoothed values advance once per sample and every channel uses them, so the
        // channels' thresholds and gains ramp together (channel-major looping left the right channel stepping).
        auto chans = buf.getArrayOfWritePointers();
        for (int i = 0; i < ns; ++i)
        {
            const float attCoeff = smoothedAtt   .getNextValue();
            const float relCoeff = smoothedRel   .getNextValue();
            const float thr      = smoothedThresh.getNextValue();
            const float og       = smoothedOut   .getNextValue();
            for (int ch = 0; ch < nCh; ++ch)
            {
                float& env = envelope[ch < 2 ? ch : 0];
                const float level = std::abs(chans[ch][i]);
                env = level > env ? attCoeff * env + (1.0f - attCoeff) * level
                                  : relCoeff * env + (1.0f - relCoeff) * level;

                float gainDb = 0.0f;
                if (env > thr && thr > 1e-8f)
                {
                    const float overDb = 20.0f * std::log10(env / thr);
                    gainDb = -overDb * (1.0f - 1.0f / ratio);
                }
                float y = chans[ch][i] * juce::Decibels::decibelsToGain(gainDb);

                // Limiter only: a brick-wall stage after the 100:1 envelope stage. The envelope follower cannot react
                // inside its attack time, so a transient would pass the ceiling; this gain is set from the sample
                // itself (instant attack) so no output sample exceeds the ceiling, and it releases over the Release
                // time so the gain does not pump at audio rate.
                if (isLimiter)
                {
                    float& lim = limGain[ch < 2 ? ch : 0];
                    const float a    = std::abs(y);
                    const float need = (a > thr && a > 1e-9f) ? thr / a : 1.0f;
                    lim = juce::jmin(need, lim + (1.0f - lim) * (1.0f - relCoeff));
                    y  *= lim;
                    if (lim < 1.0f) gainDb += 20.0f * std::log10(lim);
                }

                if (gainDb < peakGainDb) peakGainDb = gainDb;
                chans[ch][i] = y * og;
            }
        }
        // Normalise to 0..1 (1 ≡ 24 dB GR) for the UI meter.
        grOut = juce::jlimit(0.0f, 1.0f, -peakGainDb / 24.0f);
    }

private:
    double currentSampleRate = 44100.0;
    float  envelope[2] = {};
    float  limGain[2]  = { 1.0f, 1.0f };   // the limiter's instant-attack peak stage, 0..1 per channel
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedAtt;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedRel;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedThresh;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedOut;
};
