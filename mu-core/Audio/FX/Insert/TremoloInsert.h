#pragma once

#include "InsertAlgorithmBase.h"
#include <cmath>

// insertAlgo = 21. Tremolo: the level is multiplied by a low-frequency wave, so the sound pulses.
//   - Rate    0.1..20 Hz (log). With the insert's Sync on it is the length of one pulse in 1..16 sixteenth notes of the
//             tempo instead (the knob's range in sixteen equal steps); the rate follows the tempo but is not locked to
//             the song position.
//   - Depth   0..100 %: how deep the dips go (100 % dips to silence). At 0 % the sound is untouched, so Depth is also the mix.
//   - Shape   Sine, Triangle or Square. The square is rounded over about 2 ms so it does not click.
//   - Phase   0..100 %: how far the right channel's wave is behind the left; 100 % is opposite, which pans the sound
//             left and right (auto-pan).
class TremoloInsert : public InsertAlgorithmBase
{
public:
    void prepare(double sampleRate, int) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        reset();
    }

    void reset() override
    {
        phase = 0.0f;
        sq[0] = sq[1] = 1.0f;
    }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        const int nChState = juce::jmin(nCh, 2);
        // Slot 0 = Rate 0.1..20 Hz (log), 1 = Depth 0..100 %, 2 = Shape 0..2, 3 = Phase 0..100 %.
        float rate = juce::jlimit(0.1f, 20.0f, insertSlot(p, 0));
        if (p.insertSync)
        {
            const int n = mu_ui::insertSyncStepFromNorm(p.insertParam[0]);
            rate = (float) (1.0 / (n * mu_ui::insertSixteenthSeconds(tempoBpm)));
        }
        const float depth = juce::jlimit(0.0f, 1.0f, insertSlot(p, 1) * 0.01f);
        const int   shape = juce::jlimit(0, 2, (int) std::round(insertSlot(p, 2)));
        const float offs  = juce::jlimit(0.0f, 1.0f, insertSlot(p, 3) * 0.01f) * 0.5f;   // 100 % = half a turn
        const float inc   = rate / (float) sr;
        const float sqA   = 1.0f - std::exp(-1.0f / (0.002f * (float) sr));
        auto chans = buf.getArrayOfWritePointers();
        for (int i = 0; i < ns; ++i)
        {
            for (int ch = 0; ch < nChState; ++ch)
            {
                float ph = phase + (ch == 0 ? 0.0f : offs);
                if (ph >= 1.0f) ph -= 1.0f;
                float w;   // -1..1
                switch (shape)
                {
                    case 1:  w = ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph; break;   // triangle
                    case 2:  sq[ch] += ((ph < 0.5f ? 1.0f : -1.0f) - sq[ch]) * sqA; w = sq[ch]; break;   // rounded square
                    default: w = std::sin(juce::MathConstants<float>::twoPi * ph); break;
                }
                chans[ch][i] *= 1.0f - depth * 0.5f * (1.0f - w);
            }
            phase += inc;
            if (phase >= 1.0f) phase -= 1.0f;
        }
    }

private:
    double sr = 44100.0;
    float  phase = 0.0f;
    float  sq[2] = { 1.0f, 1.0f };
};
