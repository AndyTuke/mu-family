#pragma once

#include "InsertAlgorithmBase.h"
#include <cmath>

// insertAlgo = 7 (Compressor) and 8 (Limiter). Same envelope-follower
// algorithm; the only difference is the ratio (4:1 vs 100:1) which is selected
// from p.insertAlgo at process time. Sharing one class avoids duplicating
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
    void reset() override { envelope[0] = envelope[1] = 0.0f; }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& grOut) override
    {
        const float sr        = (float)currentSampleRate;
        // Slot 0 = Threshold 0..100 (mapped to 0..-40 dB), Slot 1 = Output -24..24 dB,
        // Slot 2 = Attack 0..100 (mapped to 0..200 ms), Slot 3 = Release 20..2000 ms (log).
        const float threshKnob = insertSlot(p, 0);
        const float outputDb   = insertSlot(p, 1);
        const float attackKnob = insertSlot(p, 2);
        const float relMs      = juce::jmax(1.0f, insertSlot(p, 3));
        const float threshLin  = juce::Decibels::decibelsToGain(-(threshKnob / 100.0f) * 40.0f);
        const float outGain    = juce::Decibels::decibelsToGain(outputDb);
        const float attackMs   = juce::jmax(0.1f, attackKnob * 2.0f);
        const float ratio      = (p.insertAlgo == 8) ? 100.0f : 4.0f;
        smoothedAtt   .setTargetValue(std::exp(-2.2f / (attackMs * 0.001f * sr)));
        smoothedRel   .setTargetValue(std::exp(-2.2f / (relMs    * 0.001f * sr)));
        smoothedThresh.setTargetValue(threshLin);
        smoothedOut   .setTargetValue(outGain);

        float peakGainDb = 0.0f;  // 0 or negative — fed back to UI meter via grOut
        for (int ch = 0; ch < nCh; ++ch)
        {
            auto*  data = buf.getWritePointer(ch);
            float& env  = envelope[ch < 2 ? ch : 0];
            for (int i = 0; i < ns; ++i)
            {
                // ch0 advances the ramps; ch1 reads the latest values.
                const float attCoeff = (ch == 0) ? smoothedAtt   .getNextValue()
                                                 : smoothedAtt   .getCurrentValue();
                const float relCoeff = (ch == 0) ? smoothedRel   .getNextValue()
                                                 : smoothedRel   .getCurrentValue();
                const float thr      = (ch == 0) ? smoothedThresh.getNextValue()
                                                 : smoothedThresh.getCurrentValue();
                const float og       = (ch == 0) ? smoothedOut   .getNextValue()
                                                 : smoothedOut   .getCurrentValue();

                const float level = std::abs(data[i]);
                env = level > env ? attCoeff * env + (1.0f - attCoeff) * level
                                  : relCoeff * env + (1.0f - relCoeff) * level;

                float gainDb = 0.0f;
                if (env > thr && thr > 1e-8f)
                {
                    const float overDb = 20.0f * std::log10(env / thr);
                    gainDb = -overDb * (1.0f - 1.0f / ratio);
                }
                if (gainDb < peakGainDb) peakGainDb = gainDb;
                data[i] *= juce::Decibels::decibelsToGain(gainDb) * og;
            }
        }
        // Normalise to 0..1 (1 ≡ 24 dB GR) for the UI meter.
        grOut = juce::jlimit(0.0f, 1.0f, -peakGainDb / 24.0f);
    }

private:
    double currentSampleRate = 44100.0;
    float  envelope[2] = {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedAtt;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedRel;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedThresh;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedOut;
};
