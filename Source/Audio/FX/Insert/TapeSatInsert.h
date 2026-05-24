#pragma once

#include "InsertAlgorithmBase.h"
#include "Audio/AudioFilters.h"
#include <cmath>

// insertAlgo = 10. Tape saturation — preGain → tanh → DC block → tone
// LP → output trim. The DC block sits between the non-linearity (which can
// generate DC offset for asymmetric input) and the tone filter, so the LP
// doesn't have to handle a slow drift.
class TapeSatInsert : public InsertAlgorithmBase
{
public:
    void prepare(double sampleRate, int) override
    {
        currentSampleRate = sampleRate;
        smoothedPreGain.reset(sampleRate, 0.015);  smoothedPreGain.setCurrentAndTargetValue(1.0f);
        smoothedOutGain.reset(sampleRate, 0.015);  smoothedOutGain.setCurrentAndTargetValue(1.0f);
        reset();
    }
    void reset() override
    {
        toneFilter[0].reset();
        toneFilter[1].reset();
        dcIn [0] = dcIn [1] = 0.0f;
        dcOut[0] = dcOut[1] = 0.0f;
    }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        // Slot 0 = Drive 0..100, Slot 1 = Output -24..0 dB, Slot 3 = Tone 200..20000 Hz (log).
        const float drive    = insertSlot(p, 0);
        const float outputDb = insertSlot(p, 1);
        const float toneHz   = juce::jlimit(200.0f, 20000.0f, insertSlot(p, 3));
        const float preGain  = 1.0f + (drive / 100.0f) * 9.0f;   // 1..10×
        const float outGain  = std::pow(10.0f, outputDb / 20.0f);
        const float dcCoeff = 1.0f - (2.0f * juce::MathConstants<float>::pi * 20.0f
                                      / (float)currentSampleRate);
        smoothedPreGain.setTargetValue(preGain);
        smoothedOutGain.setTargetValue(outGain);
        // toneHz also jumps per block but the OnePoleLP doesn't expose a smoothed
        // setter — leaving as-is; the audible artefact on tone-knob sweep is the
        // filter coefficient step, much smaller than the gain step.
        for (int ch = 0; ch < nCh; ++ch)
            toneFilter[ch].prepare(toneHz, (float)currentSampleRate);
        for (int ch = 0; ch < nCh; ++ch)
        {
            auto*  data = buf.getWritePointer(ch);
            float& di   = dcIn [ch < 2 ? ch : 0];
            float& dout = dcOut[ch < 2 ? ch : 0];
            for (int i = 0; i < ns; ++i)
            {
                const float pg = (ch == 0) ? smoothedPreGain.getNextValue()
                                           : smoothedPreGain.getCurrentValue();
                const float og = (ch == 0) ? smoothedOutGain.getNextValue()
                                           : smoothedOutGain.getCurrentValue();
                const float sat = std::tanh(data[i] * pg);
                const float dc  = sat - di + dcCoeff * dout;
                di   = sat;
                dout = dc;
                data[i] = toneFilter[ch].process(dc) * og;
            }
        }
    }

private:
    double    currentSampleRate = 44100.0;
    OnePoleLP toneFilter[2];
    float     dcIn [2] = {};
    float     dcOut[2] = {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedPreGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedOutGain;
};
