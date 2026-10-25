#pragma once

#include "InsertAlgorithmBase.h"
#include <cmath>
#include <vector>

// insertAlgo = 15. Tape delay (Echoplex / Space Echo character): a delay line whose repeats are re-recorded through a
// worn tape path, so each pass gets darker, thinner in the lows, softly compressed and slightly unsteady.
//   - Time     the head spacing, 20 ms..1 s. It is glided (not jumped), so turning it bends the pitch of the repeats as
//              changing the tape speed does.
//   - Feedback repeats; above 100 % the loop runs away but the tape saturation holds it (self-oscillation).
//   - Mix      dry/wet crossfade.
//   - Wear     how old the tape is: wow and flutter depth, saturation, and a lower loop treble cut.
// The loop: delayed -> low cut (~90 Hz, also blocks DC) -> two-pole treble cut (9 kHz fresh, 2.5 kHz worn) -> tanh
// saturation -> back to the input. Wow (~0.6 Hz) and flutter (~7 Hz plus a little random drift) wobble the read position,
// read with a 4-point cubic Hermite interpolator.
class TapeDelayInsert : public InsertAlgorithmBase
{
public:
    void prepare(double sampleRate, int) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        size = (int) (sr * 1.25) + 8;   // 1 s of delay plus the wobble and the interpolator's taps
        for (auto& l : line) l.assign((size_t) size, 0.0f);
        fb.reset(sr, 0.02);
        mix.reset(sr, 0.02);
        reset();
    }

    void reset() override
    {
        for (auto& l : line) std::fill(l.begin(), l.end(), 0.0f);
        write = 0;
        for (int c = 0; c < 2; ++c) { hpState[c] = lp1[c] = lp2[c] = 0.0f; drift[c] = 0.0f; }
        wowPhase = 0.0f; flutterPhase = 0.0f;
        rng[0] = 0x9e3779b9u; rng[1] = 0x7f4a7c15u;
        started = false;   // the next block takes its Time and levels as they are, with no glide from a stale value
        fb.setCurrentAndTargetValue(0.0f);
        mix.setCurrentAndTargetValue(0.0f);
    }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        if (line[0].empty()) return;
        const int nChState = juce::jmin(nCh, 2);   // state for two channels; extra channels pass through
        // Slot 0 = Time 20..1000 ms (log), 1 = Feedback 0..110 %, 2 = Mix 0..100 %, 3 = Wear 0..100 %.
        const float timeMs = juce::jlimit(20.0f, 1000.0f, insertSlot(p, 0));
        const float fbAmt  = juce::jlimit(0.0f, 1.1f, insertSlot(p, 1) * 0.01f);
        const float mixAmt = juce::jlimit(0.0f, 1.0f, insertSlot(p, 2) * 0.01f);
        const float wear   = juce::jlimit(0.0f, 1.0f, insertSlot(p, 3) * 0.01f);

        const float targetDelay = timeMs * 0.001f * (float) sr;
        if (! started)
        {
            timeSmoothed = targetDelay; started = true;
            fb.setCurrentAndTargetValue(fbAmt); mix.setCurrentAndTargetValue(mixAmt);
        }
        fb.setTargetValue(fbAmt);
        mix.setTargetValue(mixAmt);

        // One-pole glide of the delay length (~120 ms): the tape-speed pitch bend when Time moves.
        const float glide = 1.0f - std::exp(-1.0f / (0.12f * (float) sr));

        // Loop filter coefficients from Wear: the treble cut falls from 9 kHz to 2.5 kHz and the saturation drive rises.
        const float twoPi = juce::MathConstants<float>::twoPi;
        const float lpHz  = 9000.0f * std::pow(2500.0f / 9000.0f, wear);
        const float lpA   = 1.0f - std::exp(-twoPi * lpHz / (float) sr);
        const float hpA   = 1.0f - std::exp(-twoPi * 90.0f / (float) sr);
        const float satG  = 1.0f + wear * 1.5f;

        // Wow and flutter depth as a fraction of the delay (a longer delay is a longer tape run, so it wobbles more).
        const float wowDepth     = wear * 0.0035f;
        const float flutterDepth = wear * 0.0009f;
        const float wowInc       = 0.6f / (float) sr;
        const float flutterInc   = 7.0f / (float) sr;
        const float driftA       = 1.0f - std::exp(-twoPi * 3.0f / (float) sr);

        auto chans = buf.getArrayOfWritePointers();
        for (int i = 0; i < ns; ++i)
        {
            timeSmoothed += (targetDelay - timeSmoothed) * glide;
            const float f = fb.getNextValue();
            const float m = mix.getNextValue();
            wowPhase += wowInc;           if (wowPhase >= 1.0f)     wowPhase -= 1.0f;
            flutterPhase += flutterInc;   if (flutterPhase >= 1.0f) flutterPhase -= 1.0f;

            for (int ch = 0; ch < nChState; ++ch)
            {
                // The right channel's wobble is offset so a stereo source gets a slight tape-machine width.
                const float off = ch == 0 ? 0.0f : 0.31f;
                const float wow = std::sin(twoPi * (wowPhase + off));
                const float flu = std::sin(twoPi * (flutterPhase + off * 1.7f));
                drift[ch] += (nextNoise(ch) - drift[ch]) * driftA;
                const float wobble = 1.0f + wowDepth * wow + flutterDepth * (flu + 1.5f * drift[ch]);
                const float d = juce::jlimit(4.0f, (float) (size - 8), timeSmoothed * wobble);

                // Read d samples behind the write head with a 4-point Hermite interpolator.
                const float rp = (float) write - d;
                const int   i1 = (int) std::floor(rp);
                const float fr = rp - (float) i1;
                const auto& l = line[ch];
                const float y0 = l[(size_t) wrap(i1 - 1)], y1 = l[(size_t) wrap(i1)],
                            y2 = l[(size_t) wrap(i1 + 1)], y3 = l[(size_t) wrap(i1 + 2)];
                const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3,
                            c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                const float delayed = ((c3 * fr + c2) * fr + c1) * fr + y1;

                // The tape path: low cut, two-pole treble cut, then saturation.
                hpState[ch] += (delayed - hpState[ch]) * hpA;
                const float x = delayed - hpState[ch];
                lp1[ch] += (x - lp1[ch]) * lpA;
                lp2[ch] += (lp1[ch] - lp2[ch]) * lpA;
                const float wet = std::tanh(lp2[ch] * satG) / satG;

                const float in = chans[ch][i];
                line[ch][(size_t) write] = in + wet * f;
                chans[ch][i] = in * (1.0f - m) + wet * m;
            }
            if (++write >= size) write = 0;
        }
    }

private:
    int wrap(int i) const noexcept { while (i < 0) i += size; while (i >= size) i -= size; return i; }

    // A white-noise sample in -1..1 (xorshift32), one generator per channel, deterministic.
    float nextNoise(int ch) noexcept
    {
        auto& s = rng[ch];
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float) (int) s * (1.0f / 2147483648.0f);
    }

    double sr = 44100.0;
    int    size = 0, write = 0;
    std::vector<float> line[2];
    float  hpState[2] = {}, lp1[2] = {}, lp2[2] = {}, drift[2] = {};
    float  wowPhase = 0.0f, flutterPhase = 0.0f, timeSmoothed = 0.0f;
    unsigned int rng[2] = { 0x9e3779b9u, 0x7f4a7c15u };
    bool   started = false;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> fb, mix;
};
