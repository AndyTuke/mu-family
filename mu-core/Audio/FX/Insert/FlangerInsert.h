#pragma once

#include "InsertAlgorithmBase.h"
#include "InsertDelayLine.h"
#include <cmath>

// insertAlgo = 18. Flanger: a very short delay (0.3..5.3 ms) swept by a slow sine and fed back on itself, so the comb
// filter it makes sweeps up and down the spectrum with a jet-plane whoosh.
//   - Rate      LFO speed, 0.05..5 Hz.
//   - Depth     how far the delay sweeps (0 holds it at 0.3 ms).
//   - Feedback  -95..95 %: how much of the delayed signal is fed back in; a negative value inverts it for a hollower tone.
//   - Mix       dry/wet crossfade.
// The fed-back signal passes through tanh, so even 95 % cannot run away. The right channel's LFO is a quarter turn behind.
class FlangerInsert : public InsertAlgorithmBase
{
public:
    void prepare(double sampleRate, int) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        for (auto& l : line) l.prepare(sr, 0.02);
        mix.reset(sr, 0.02);
        fb.reset(sr, 0.02);
        reset();
    }

    void reset() override
    {
        for (auto& l : line) l.clear();
        phase = 0.0f;
        mix.setCurrentAndTargetValue(0.0f);
        fb.setCurrentAndTargetValue(0.0f);
        started = false;
    }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        if (! line[0].ready()) return;
        const int nChState = juce::jmin(nCh, 2);
        // Slot 0 = Rate 0.05..5 Hz (log), 1 = Depth 0..100 %, 2 = Feedback -95..95 %, 3 = Mix 0..100 %.
        const float rate   = juce::jlimit(0.05f, 5.0f, insertSlot(p, 0));
        const float depth  = juce::jlimit(0.0f, 1.0f, insertSlot(p, 1) * 0.01f);
        const float fbAmt  = juce::jlimit(-0.95f, 0.95f, insertSlot(p, 2) * 0.01f);
        const float mixAmt = juce::jlimit(0.0f, 1.0f, insertSlot(p, 3) * 0.01f);
        if (! started) { mix.setCurrentAndTargetValue(mixAmt); fb.setCurrentAndTargetValue(fbAmt); started = true; }
        mix.setTargetValue(mixAmt);
        fb.setTargetValue(fbAmt);

        const float inc    = rate / (float) sr;
        const float minDel = 0.0003f * (float) sr;
        const float span   = 0.005f * (float) sr * depth;
        auto chans = buf.getArrayOfWritePointers();
        for (int i = 0; i < ns; ++i)
        {
            const float m = mix.getNextValue();
            const float f = fb.getNextValue();
            for (int ch = 0; ch < nChState; ++ch)
            {
                const float ph  = phase + (ch == 0 ? 0.0f : 0.25f);
                const float lfo = 0.5f + 0.5f * std::sin(juce::MathConstants<float>::twoPi * ph);
                const float in  = chans[ch][i];
                const float delayed = line[ch].read(minDel + span * lfo);
                line[ch].push(std::tanh(in + delayed * f));
                chans[ch][i] = in * (1.0f - m) + delayed * m;
            }
            phase += inc;
            if (phase >= 1.0f) phase -= 1.0f;
        }
    }

private:
    double sr = 44100.0;
    InsertDelayLine line[2];
    float phase = 0.0f;
    bool  started = false;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mix, fb;
};
