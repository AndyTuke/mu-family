#pragma once

#include "Audio/FX/SpringReverb.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// A three-head tape echo with a spring reverb, after the classic tape-echo-and-spring unit: one record head and three equally spaced
// playback heads on a variable-speed loop, a twelve-position mode selector choosing which heads sound and whether the spring is
// added, Repeat Rate (the tape speed), Intensity (the repeats fed back), Echo and Reverb volumes, Bass and Treble, a drive that
// overloads the repeats only, and tape hiss. Every repeat is darker than the last (the loop loses highs and lows), a slow wow and a
// fast flutter move the tape, and a rate change bends the pitch of what is on the tape, as the real motor does. The reverb is fed
// from the input, outside the tape loop. Mono in, a stereo pair of the WET signal out (the caller adds the dry). Header-only;
// prepare() allocates, process() never does.
namespace mu_core
{

class TapeSpringEcho
{
public:
    static constexpr int kModes = 12;

    struct Params
    {
        int   mode      = 1;       // 1..12, see modeHeads / modeReverb
        float rate      = 0.5f;    // 0 slow tape (long repeats) .. 1 fast tape (short repeats)
        float intensity = 0.3f;    // 0..1: how much of the repeats is fed back (the top end runs into self-oscillation, held by the loop's soft clip)
        float echo      = 0.7f;    // 0..1 echo level
        float reverb    = 0.5f;    // 0..1 spring level
        float bass      = 0.0f;    // -1..1: low shelf, about +-12 dB
        float treble    = 0.0f;    // -1..1: high shelf, about +-12 dB
        float drive     = 0.0f;    // 0..1: overload of the repeats (the dry is never touched)
        float hiss      = 0.0f;    // 0..1: tape hiss under the repeats
        float spacingSeconds = 0.0f;   // > 0: the head spacing in seconds, in place of the rate (tempo sync)
    };

    // Which of the three heads sound in a mode (bit 0 head 1, bit 1 head 2, bit 2 head 3). Modes 1-3 are the heads singly, 4 is heads 2
    // and 3, 5-7 are the heads singly with the spring, 8-10 are the pairs 1+2, 1+3, 2+3 with the spring, 11 is all three with the
    // spring, 12 is the spring alone.
    static int  modeHeads(int mode) noexcept
    {
        static constexpr int heads[kModes] = { 1, 2, 4, 6, 1, 2, 4, 3, 5, 6, 7, 0 };
        return heads[std::clamp(mode, 1, kModes) - 1];
    }
    static bool modeReverb(int mode) noexcept { return std::clamp(mode, 1, kModes) >= 5; }

    // The distance between two heads, in seconds, at a Repeat Rate: head 1 is one spacing from the record head, head 3 is three.
    static float headSpacingSeconds(float rate) noexcept
    {
        return 0.045f * std::pow(5.0f, 1.0f - std::clamp(std::isfinite(rate) ? rate : 0.5f, 0.0f, 1.0f));
    }

    void prepare(double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        line.assign((size_t) (sr * 6.2) + 16, 0.0f);   // three heads at 16 sixteenths of 120 bpm (2 s apart), and the rate range
        spring.prepare(sr);
        spring.setParameters(2.2f, 0.55f, 0.6f, 0.5f);
        smooth = 1.0f - (float) std::exp(-1.0 / (0.15 * sr));   // the tape motor's inertia: about 150 ms to follow a rate change
        loopLp  = 1.0f - (float) std::exp(-2.0 * kPi * 5200.0 / sr);
        loopHp  = 1.0f - (float) std::exp(-2.0 * kPi * 90.0 / sr);
        reset();
        setParameters(params);
        spacing = targetSpacing;
    }

    void reset() noexcept
    {
        std::fill(line.begin(), line.end(), 0.0f);
        write = 0;
        lpState = hpState = 0.0f;
        wowPhase = 0.0; flutterPhase = 0.0;
        rng = 0x1234567u;
        for (auto& s : shelfLow) s.reset();
        for (auto& s : shelfHigh) s.reset();
        spring.reset();
    }

    void setParameters(const Params& p) noexcept
    {
        auto fin = [](float v, float lo, float hi, float def) { return std::clamp(std::isfinite(v) ? v : def, lo, hi); };
        params.mode      = std::clamp(p.mode, 1, kModes);
        params.rate      = fin(p.rate, 0.0f, 1.0f, 0.5f);
        params.intensity = fin(p.intensity, 0.0f, 1.0f, 0.3f);
        params.echo      = fin(p.echo, 0.0f, 1.0f, 0.7f);
        params.reverb    = fin(p.reverb, 0.0f, 1.0f, 0.5f);
        params.bass      = fin(p.bass, -1.0f, 1.0f, 0.0f);
        params.treble    = fin(p.treble, -1.0f, 1.0f, 0.0f);
        params.drive     = fin(p.drive, 0.0f, 1.0f, 0.0f);
        params.hiss      = fin(p.hiss, 0.0f, 1.0f, 0.0f);
        params.spacingSeconds = fin(p.spacingSeconds, 0.0f, 60.0f, 0.0f);
        targetSpacing = headSpacingSeconds(params.rate) * (float) sr;
        if (params.spacingSeconds > 0.0f)   // synced: the third head must still fit in the loop
            targetSpacing = std::min(params.spacingSeconds * (float) sr, std::max(0.0f, (float) line.size() - 24.0f) / 3.0f);
        for (auto& s : shelfLow)  s.setLowShelf(sr, 110.0, 12.0 * params.bass);
        for (auto& s : shelfHigh) s.setHighShelf(sr, 3500.0, 12.0 * params.treble);
    }

    // Moves the tape speed to its target at once (the first block after a prepare or reset starts there instead of gliding).
    void jumpToTarget() noexcept { spacing = targetSpacing; }

    // One sample in, the wet pair out.
    void process(float in, float& outL, float& outR) noexcept
    {
        if (line.empty()) { outL = outR = 0.0f; return; }
        if (! std::isfinite(in)) in = 0.0f;

        // The tape speed glides to its target, so turning Repeat Rate bends the repeats' pitch.
        spacing += smooth * (targetSpacing - spacing);

        // Wow (slow) and flutter (fast): a small movement of the tape, as a delay modulation in samples.
        wowPhase += 0.55 / sr;     if (wowPhase >= 1.0) wowPhase -= 1.0;
        flutterPhase += 6.3 / sr;  if (flutterPhase >= 1.0) flutterPhase -= 1.0;
        const float move = (float) (0.00028 * sr * std::sin(2.0 * kPi * wowPhase) + 0.00005 * sr * std::sin(2.0 * kPi * flutterPhase));

        // The playback heads: three taps of the loop at one, two and three spacings.
        const int heads = modeHeads(params.mode);
        float sum = 0.0f;
        int count = 0;
        for (int h = 0; h < 3; ++h)
            if ((heads >> h) & 1) { sum += tap(spacing * (float) (h + 1) + move); ++count; }
        const float norm = count > 0 ? 1.0f / std::sqrt((float) count) : 0.0f;
        const float repeats = sum * norm;

        // The loop: the repeats are overdriven (drive), lose highs and lows (the tape), and return scaled by Intensity.
        float fb = std::tanh(repeats * (1.0f + 5.0f * params.drive)) / (1.0f + 1.5f * params.drive);
        lpState += loopLp * (fb - lpState);
        hpState += loopHp * (lpState - hpState);
        fb = lpState - hpState;
        float next = in + fb * params.intensity * 1.05f;
        next = std::tanh(next * 0.9f) / 0.9f;                          // the tape saturates before the loop can run away
        if (std::abs(next) < 1.0e-20f) next = 0.0f;                    // flush denormals out of the recirculating tail
        line[(size_t) write] = next;
        if (++write >= (int) line.size()) write = 0;

        // Hiss under the repeats, then the echo level.
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        const float noise = ((float) (rng & 0xffff) / 32768.0f - 1.0f) * 0.004f * params.hiss * params.hiss;
        const float echoOut = (count > 0 ? (repeats + noise) : 0.0f) * params.echo;

        // The spring takes the input, outside the tape loop, in the modes that have it.
        float rl = 0.0f, rr = 0.0f;
        if (modeReverb(params.mode)) { spring.process(in, rl, rr); rl *= params.reverb * 2.0f; rr *= params.reverb * 2.0f; }
        else spring.process(0.0f, rl, rr), rl = rr = 0.0f;   // keep the tank ticking so a later mode change finds it settled

        // Bass and treble over the whole wet signal.
        float l = echoOut + rl, r = echoOut + rr;
        l = shelfHigh[0].process(shelfLow[0].process(l));
        r = shelfHigh[1].process(shelfLow[1].process(r));
        outL = l; outR = r;
    }

private:
    static constexpr double kPi = 3.14159265358979323846;

    // One tap of the loop `delaySamples` behind the write point (linear interpolation).
    float tap(float delaySamples) const noexcept
    {
        const int size = (int) line.size();
        const float d = std::clamp(delaySamples, 2.0f, (float) (size - 4));
        float rp = (float) write - d;
        if (rp < 0.0f) rp += (float) size;
        const int i0 = (int) rp;
        const float f = rp - (float) i0;
        const int i1 = i0 + 1 >= size ? 0 : i0 + 1;
        return line[(size_t) i0] + f * (line[(size_t) i1] - line[(size_t) i0]);
    }

    // A shelving biquad (RBJ cookbook), one per channel and band.
    struct Shelf
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void reset() noexcept { z1 = z2 = 0.0f; }
        void setLowShelf(double sr, double hz, double dB) noexcept  { set(sr, hz, dB, false); }
        void setHighShelf(double sr, double hz, double dB) noexcept { set(sr, hz, dB, true); }
        void set(double sr, double hz, double dB, bool high) noexcept
        {
            const double A = std::pow(10.0, dB / 40.0), w = 2.0 * kPi * hz / sr, cw = std::cos(w), sw = std::sin(w);
            const double alpha = sw / 2.0 * std::sqrt(2.0);   // shelf slope 1
            const double sA = 2.0 * std::sqrt(A) * alpha;
            double B0, B1, B2, A0, A1, A2;
            if (! high)
            {
                B0 = A * ((A + 1) - (A - 1) * cw + sA);  B1 = 2 * A * ((A - 1) - (A + 1) * cw);  B2 = A * ((A + 1) - (A - 1) * cw - sA);
                A0 = (A + 1) + (A - 1) * cw + sA;        A1 = -2 * ((A - 1) + (A + 1) * cw);     A2 = (A + 1) + (A - 1) * cw - sA;
            }
            else
            {
                B0 = A * ((A + 1) + (A - 1) * cw + sA);  B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - sA);
                A0 = (A + 1) - (A - 1) * cw + sA;        A1 = 2 * ((A - 1) - (A + 1) * cw);      A2 = (A + 1) - (A - 1) * cw - sA;
            }
            b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0); a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
        }
        float process(float x) noexcept   // transposed direct form II
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    double sr = 44100.0;
    Params params;
    std::vector<float> line;
    int   write = 0;
    float spacing = 0.0f, targetSpacing = 0.0f, smooth = 0.0f, loopLp = 0.0f, loopHp = 0.0f;
    float lpState = 0.0f, hpState = 0.0f;
    double wowPhase = 0.0, flutterPhase = 0.0;
    uint32_t rng = 1u;
    std::array<Shelf, 2> shelfLow, shelfHigh;
    SpringReverb spring;
};

}
