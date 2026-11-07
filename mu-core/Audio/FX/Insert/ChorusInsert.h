#pragma once

#include "InsertAlgorithmBase.h"
#include "InsertDelayLine.h"
#include <cmath>

// insertAlgo = 17. Chorus: the input is mixed with a copy read from a short delay line whose delay is swept by a low-
// frequency sine, which detunes the copy slightly and slowly, thickening the sound.
//   - Rate    LFO speed, 0.05..8 Hz.
//   - Depth   how far the delay is swept, as a share of the Delay time.
//   - Delay   the centre delay, 5..30 ms.
//   - Mix     dry/wet crossfade.
// The right channel's LFO runs a quarter turn behind the left, so a stereo output widens.
class ChorusInsert : public InsertAlgorithmBase
{
public:
    void prepare(double sampleRate, int) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        for (auto& l : line) l.prepare(sr, 0.06);
        mix.reset(sr, 0.02);
        reset();
    }

    void reset() override
    {
        for (auto& l : line) l.clear();
        phase = 0.0f;
        mix.setCurrentAndTargetValue(0.0f);
        started = false;
    }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        if (! line[0].ready()) return;
        const int nChState = juce::jmin(nCh, 2);
        // Slot 0 = Rate 0.05..8 Hz (log), 1 = Depth 0..100 %, 2 = Delay 5..30 ms, 3 = Mix 0..100 %.
        const float rate    = juce::jlimit(0.05f, 8.0f, insertSlot(p, 0));
        const float depth   = juce::jlimit(0.0f, 1.0f, insertSlot(p, 1) * 0.01f);
        const float centre  = juce::jlimit(5.0f, 30.0f, insertSlot(p, 2)) * 0.001f * (float) sr;
        const float mixAmt  = juce::jlimit(0.0f, 1.0f, insertSlot(p, 3) * 0.01f);
        if (! started) { mix.setCurrentAndTargetValue(mixAmt); started = true; }
        mix.setTargetValue(mixAmt);

        const float inc = rate / (float) sr;
        const float swing = depth * 0.4f * centre;   // at most 40 % of the centre delay either side
        auto chans = buf.getArrayOfWritePointers();
        for (int i = 0; i < ns; ++i)
        {
            const float m = mix.getNextValue();
            for (int ch = 0; ch < nChState; ++ch)
            {
                const float ph  = phase + (ch == 0 ? 0.0f : 0.25f);
                const float lfo = std::sin(juce::MathConstants<float>::twoPi * ph);
                const float in  = chans[ch][i];
                const float wet = line[ch].read(centre + swing * lfo);
                line[ch].push(in);
                chans[ch][i] = in * (1.0f - m) + wet * m;
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
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mix;
};
