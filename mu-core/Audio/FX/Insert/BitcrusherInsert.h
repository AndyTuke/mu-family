#pragma once

#include "InsertAlgorithmBase.h"
#include "Audio/AudioFilters.h"
#include <cmath>

// insertAlgo = 4. Bitcrusher — bit-depth quantisation + sample-rate
// reduction + TPDF dither. Anti-aliasing LP guards against fold-back from the
// sample-rate decimation. Per-channel counters track the position within each
// hold cell.
class BitcrusherInsert : public InsertAlgorithmBase
{
public:
    void prepare(double sampleRate, int) override
    {
        currentSampleRate = sampleRate;
        reset();
    }
    void reset() override
    {
        bitAaFilter[0].reset();
        bitAaFilter[1].reset();
        cnt [0] = cnt [1] = 0.0f;
        held[0] = held[1] = 0.0f;
    }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        // Slot 0 = Bits 1..16 (int step), Slot 1 = Rate 100..48000 Hz (log),
        // Slot 2 = Dither 0..100 %, Slot 3 (LPF) is applied downstream.
        const float bits    = juce::jlimit(1.0f, 16.0f, insertSlot(p, 0));
        const float rateHz  = juce::jmax(100.0f, insertSlot(p, 1));
        const float ditherP = insertSlot(p, 2);
        const float q       = std::pow(2.0f, bits - 1.0f);
        const float ratioF  = juce::jmax(1.0f,
            (float)(currentSampleRate / (double) rateHz));
        const float dither  = ditherP / 100.0f * (0.5f / q);
        const float aaCut   = juce::jmin(rateHz * 0.45f,
                                         (float)currentSampleRate * 0.49f);

        for (int ch = 0; ch < nCh; ++ch)
            bitAaFilter[ch].prepare(aaCut, (float)currentSampleRate);

        for (int ch = 0; ch < nCh; ++ch)
        {
            auto*  data = buf.getWritePointer(ch);
            float& c    = cnt [ch < 2 ? ch : 0];
            float& h    = held[ch < 2 ? ch : 0];

            for (int i = 0; i < ns; ++i)
            {
                const float filtered = ratioF > 1.0f ? bitAaFilter[ch].process(data[i])
                                                     : data[i];
                c += 1.0f;
                if (c >= ratioF)
                {
                    const float r1 = rng.nextFloat();
                    const float r2 = rng.nextFloat();
                    h = std::round((filtered + (r1 - r2) * dither) * q) / q;
                    c -= ratioF;
                }
                data[i] = h;
            }
        }
    }

private:
    double       currentSampleRate = 44100.0;
    OnePoleLP    bitAaFilter[2];
    float        cnt [2] = {};
    float        held[2] = {};
    juce::Random rng;
};
