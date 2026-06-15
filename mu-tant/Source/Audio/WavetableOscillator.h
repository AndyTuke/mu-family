#pragma once

#include "WavetableBank.h"
#include <cmath>

// Single wavetable oscillator (design-voice.md "Wavetable oscillator").
// Free-running phase (never reset on its own — mu-tant is a drone), morph
// position scan with linear cross-fade between adjacent frames, and a phase-mod
// input so the voice can FM one osc from the other. `justWrapped()` lets the
// voice hard-sync a partner osc.
namespace mu_tant
{

class WavetableOscillator
{
public:
    void prepare(double sampleRate) noexcept { sr = sampleRate > 0 ? sampleRate : 44100.0; }
    void setBank(const WavetableBank* b) noexcept { bank = b; }
    void setTable(int t) noexcept { tableIndex = t < 0 ? 0 : t; }
    void setFrequency(float hz) noexcept { inc = (double) hz / sr; }
    void setPosition(float p01) noexcept { position = p01 < 0 ? 0.0f : (p01 > 1 ? 1.0f : p01); }
    void resetPhase() noexcept { phase = 0.0; }

    // Render one sample and advance.
    //   phaseOffset : added phase offset in cycles (phase-modulation / PM).
    //   incMul      : per-sample multiplier on the base increment (frequency modulation /
    //                 FM, TZFM). 1.0 = unmodulated. May be negative for through-zero FM,
    //                 where the phase runs backwards. The mip level still keys off the base
    //                 |inc| so anti-aliasing stays stable under modulation.
    // Sets the wrap flag (forward 1.0 crossing only) readable via justWrapped() for sync.
    float render(float phaseOffset = 0.0f, double incMul = 1.0) noexcept
    {
        if (bank == nullptr) return 0.0f;
        const int nf = bank->numFrames(tableIndex);
        if (nf <= 0) return 0.0f;

        const float ph = (float) phase + phaseOffset;

        // cross-fade between the two morph frames straddling `position`.
        const float framePos = position * (float) (nf - 1);
        const int   f0       = (int) framePos;
        const float fFrac    = framePos - (float) f0;
        const float s0       = bank->frameSample(tableIndex, inc, f0,     ph);
        const float s1       = bank->frameSample(tableIndex, inc, f0 + 1, ph);
        const float out      = s0 + fFrac * (s1 - s0);

        phase += inc * incMul;
        wrapped = (phase >= 1.0);                 // forward wrap → drives hard-sync
        phase  -= std::floor(phase);              // wrap into [0,1) either direction

        return out;
    }

    bool justWrapped() const noexcept { return wrapped; }

private:
    const WavetableBank* bank = nullptr;
    int    tableIndex = 0;
    double sr       = 44100.0;
    double phase    = 0.0;
    double inc      = 0.0;
    float  position = 0.0f;
    bool   wrapped  = false;
};

} // namespace mu_tant
