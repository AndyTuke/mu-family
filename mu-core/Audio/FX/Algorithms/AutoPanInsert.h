#pragma once

#include "Audio/FX/Insert/InsertAlgorithmBase.h"
#include "Audio/FX/Algorithms/FxUse.h"
#include <cmath>

namespace mu_audio
{
// insertAlgo = 22. Auto Pan: a low-frequency wave swings the sound between left and right with a constant-power law, so the
// loudness stays even as it moves (Tremolo's Phase 100 % pans too, but on a linear law that dips 6 dB in the centre).
//   - Rate    0.1..20 Hz (log). With the insert's Sync on it is the length of one sweep in 1..16 sixteenth notes of the tempo.
//   - Depth   0..100 %: how far the wave swings the pan from Centre (100 % reaches full left and right).
//   - Shape   Sine, Triangle or Square (the square is rounded over about 2 ms so it does not click).
//   - Centre  -100..100 %: a static pan offset; at Depth 0 it is a plain pan control.
// pan = clamp(centre + depth * wave, -1, 1); the gains are sqrt(2) * cos / sin of (pan + 1) * pi/4, so both are exactly 1 in the
// middle (Depth 0 with Centre 0 leaves the signal untouched). The gains are a balance on the left and right channels, not a
// mono-to-stereo spread; a mono layer passes through unchanged. No latency.
class AutoPanInsert : public InsertAlgorithmBase
{
public:
    static constexpr FxUse kUse = FxUse::Insert;

    void prepare(double sampleRate, int) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        depthSm.reset(sr, 0.015);
        centreSm.reset(sr, 0.015);
        reset();
    }

    void reset() override
    {
        phase = 0.0f;
        sq = 1.0f;
        primed = false;   // the smoothers snap to their target on the next block, as a fresh instance does
    }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        if (nCh < 2 || ns <= 0) return;   // mono: nothing to balance
        // Slot 0 = Rate, 1 = Depth 0..100 %, 2 = Shape 0..2, 3 = Centre -100..100 %. A NaN knob falls back to its default.
        auto finiteOr = [](float v, float fb) { return std::isfinite(v) ? v : fb; };
        float rate = juce::jlimit(0.1f, 20.0f, finiteOr(insertSlot(p, 0), 0.5f));
        if (p.insertSync)
        {
            const double n = mu_ui::rateSyncSixteenthsFromNorm(p.insertParam[0]);
            rate = (float) (1.0 / (n * mu_ui::insertSixteenthSeconds(tempoBpm)));
        }
        const float depth  = juce::jlimit(0.0f, 1.0f,  finiteOr(insertSlot(p, 1), 60.0f) * 0.01f);
        const int   shape  = juce::jlimit(0, 2, (int) std::round(finiteOr(insertSlot(p, 2), 0.0f)));
        const float centre = juce::jlimit(-1.0f, 1.0f, finiteOr(insertSlot(p, 3), 0.0f) * 0.01f);
        if (! primed)
        {
            depthSm.setCurrentAndTargetValue(depth);
            centreSm.setCurrentAndTargetValue(centre);
            primed = true;
        }
        depthSm.setTargetValue(depth);
        centreSm.setTargetValue(centre);

        const float inc = rate / (float) sr;
        const float sqA = 1.0f - std::exp(-1.0f / (0.002f * (float) sr));
        constexpr float kSqrt2 = 1.41421356f;
        auto chans = buf.getArrayOfWritePointers();
        // Per sample: advance the wave, turn it into a pan position, then apply the constant-power gains to both channels.
        for (int i = 0; i < ns; ++i)
        {
            float w;   // -1..1
            switch (shape)
            {
                case 1:  w = phase < 0.5f ? 4.0f * phase - 1.0f : 3.0f - 4.0f * phase; break;   // triangle
                case 2:  sq += ((phase < 0.5f ? 1.0f : -1.0f) - sq) * sqA; w = sq; break;        // rounded square
                default: w = std::sin(juce::MathConstants<float>::twoPi * phase); break;
            }
            const float pan = juce::jlimit(-1.0f, 1.0f, centreSm.getNextValue() + depthSm.getNextValue() * w);
            float gL = 1.0f, gR = 1.0f;
            if (std::abs(pan) > 1.0e-7f)   // exactly 1 in the middle, so an untouched signal stays bit-exact
            {
                const float a = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
                gL = kSqrt2 * std::cos(a);
                gR = kSqrt2 * std::sin(a);
            }
            // A non-finite sample is taken as silence so it cannot propagate through the gains.
            const float l = chans[0][i], r = chans[1][i];
            chans[0][i] = std::isfinite(l) ? l * gL : 0.0f;
            chans[1][i] = std::isfinite(r) ? r * gR : 0.0f;
            phase += inc;
            if (phase >= 1.0f) phase -= 1.0f;
        }
    }

private:
    double sr = 44100.0;
    float  phase = 0.0f;
    float  sq = 1.0f;
    bool   primed = false;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> depthSm, centreSm;
};
}  // namespace mu_audio
