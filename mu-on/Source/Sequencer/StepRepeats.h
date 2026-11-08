#pragma once

#include <algorithm>
#include <cmath>

// A step's repeats (Elektron "retrig"): after its first hit a step can play up to 16 more at a note-value rate, with a Fade across
// them. Pure arithmetic, so it is unit-tested headless; Track schedules the hits it plans.
namespace mu_on
{
namespace step_repeats
{
    constexpr int kMaxRepeats = 16;
    constexpr int kNumRates = 8;
    constexpr int kDefaultRate = 3;   // 1/16

    // The time between repeats in beats, by rate index: 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T, 1/64.
    constexpr double rateBeats(int index) noexcept
    {
        constexpr double beats[kNumRates] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0, 0.0625 };
        return beats[std::clamp(index, 0, kNumRates - 1)];
    }
    inline const char* rateName(int index) noexcept
    {
        static const char* const names[kNumRates] = { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64" };
        return names[std::clamp(index, 0, kNumRates - 1)];
    }

    // The level of hit `k` (0 = the step's own first hit, 1..count the repeats) as a share of the step's velocity. `fade` is
    // -100..+100 percent: negative fades out (the last repeat plays at 1 - |fade| of the first), positive fades in (the first
    // hit plays at 1 - fade of the last), 0 is flat. The ends are the first hit and the last repeat.
    inline float gain(int k, int count, int fade) noexcept
    {
        if (count <= 0) return 1.0f;
        const float f = std::clamp(fade, -100, 100) * 0.01f;
        const float start = f > 0.0f ? 1.0f - f : 1.0f;
        const float end   = f < 0.0f ? 1.0f + f : 1.0f;
        return start + (end - start) * (float) std::clamp(k, 0, count) / (float) count;
    }

    // The beats of the repeats after a first hit at `firstBeat`: `count` of them, `rate` beats apart, stopping before `limitBeat`
    // (the next step that is on: a repeat run ends when another step comes). Returns how many were planned; beats[k - 1] is hit k.
    inline int plan(double firstBeat, int count, double rate, double limitBeat, double* beats) noexcept
    {
        int planned = 0;
        for (int k = 1; k <= std::clamp(count, 0, kMaxRepeats); ++k)
        {
            const double beat = firstBeat + k * rate;
            if (beat >= limitBeat - 1.0e-9) break;
            beats[planned++] = beat;
        }
        return planned;
    }
}
}
