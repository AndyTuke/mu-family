#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <vector>

// A mono circular delay line read with a 4-point cubic Hermite interpolator (fractional delay, smooth when the delay
// moves). Shared by the modulated-delay inserts (Chorus, Flanger, Pitch Delay). Allocation happens in prepare() only.
class InsertDelayLine
{
public:
    // Room for `maxSeconds` of delay plus the interpolator's taps.
    void prepare(double sampleRate, double maxSeconds)
    {
        size = (int) (sampleRate * maxSeconds) + 8;
        buf.assign((size_t) size, 0.0f);
        write = 0;
    }

    void clear() { std::fill(buf.begin(), buf.end(), 0.0f); write = 0; }

    bool ready() const noexcept { return size > 0; }
    int  capacity() const noexcept { return size; }

    // Store the newest sample; call once per sample after the reads for that sample.
    void push(float x) noexcept
    {
        buf[(size_t) write] = x;
        if (++write >= size) write = 0;
    }

    // The sample `delaySamples` behind the newest one (clamped to the line's range).
    float read(float delaySamples) const noexcept
    {
        const float d  = std::clamp(delaySamples, 1.0f, (float) (size - 4));
        const float rp = (float) write - d;
        const int   i1 = (int) std::floor(rp);
        const float fr = rp - (float) i1;
        const float y0 = buf[(size_t) wrap(i1 - 1)], y1 = buf[(size_t) wrap(i1)],
                    y2 = buf[(size_t) wrap(i1 + 1)], y3 = buf[(size_t) wrap(i1 + 2)];
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3,
                    c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * fr + c2) * fr + c1) * fr + y1;
    }

private:
    int wrap(int i) const noexcept { while (i < 0) i += size; while (i >= size) i -= size; return i; }

    std::vector<float> buf;
    int size = 0, write = 0;
};
