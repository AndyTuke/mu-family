#include "WavetableBank.h"
#include <cmath>
#include <algorithm>

namespace mu_tant
{

void WavetableBank::generateBuiltIn(int numFrames, int tableSize, int maxHarmonics)
{
    frames = std::max(1, numFrames);
    size   = std::max(4, tableSize);
    data.assign((size_t) frames * (size_t) size, 0.0f);

    constexpr double kTwoPi = 6.283185307179586;
    // Harmonic count is capped below the table's own Nyquist (size/2) so the
    // single-cycle frame itself is well-sampled.
    const int kMax = std::min(maxHarmonics, size / 2 - 1);

    for (int f = 0; f < frames; ++f)
    {
        // morph 0 (pure sine) -> 1 (band-limited saw). Higher harmonics fade in
        // with the morph; the fundamental is always present.
        const double morph = (frames > 1) ? (double) f / (double) (frames - 1) : 0.0;

        float* frameData = &data[(size_t) f * (size_t) size];
        float  peak = 0.0f;
        for (int i = 0; i < size; ++i)
        {
            const double x = (double) i / (double) size;   // phase 0..1
            double acc = std::sin(kTwoPi * x);              // fundamental
            for (int k = 2; k <= kMax; ++k)
                acc += (morph / (double) k) * std::sin(kTwoPi * (double) k * x);
            frameData[i] = (float) acc;
            peak = std::max(peak, std::abs(frameData[i]));
        }
        // Normalise each frame to unit peak so morphing doesn't change level.
        if (peak > 1.0e-6f)
        {
            const float g = 1.0f / peak;
            for (int i = 0; i < size; ++i) frameData[i] *= g;
        }
    }
}

float WavetableBank::frameSample(int f, float phase01) const noexcept
{
    if (data.empty()) return 0.0f;
    f = std::clamp(f, 0, frames - 1);

    // wrap phase into [0,1)
    phase01 -= std::floor(phase01);
    const float pos  = phase01 * (float) size;
    int         i0   = (int) pos;
    const float frac = pos - (float) i0;
    if (i0 >= size) i0 = size - 1;
    const int   i1   = (i0 + 1) % size;

    const float* fr = &data[(size_t) f * (size_t) size];
    return fr[i0] + frac * (fr[i1] - fr[i0]);
}

} // namespace mu_tant
