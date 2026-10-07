#pragma once

#include <juce_core/juce_core.h>
#include "WavetableOscillator.h"
#include "HilbertTransform.h"
#include <cmath>

// Two wavetable oscillators with the family's 2-lane cross-modulation (docs/mu-tant/
// mu-tant-xmod-design.md), shared by mu-Tant and mu-Toni. Osc 2 is the modulator, Osc 1 the
// carrier:
//   Lane A (phase / index): FM, PM or through-zero FM of Osc 1 by Osc 2, plus hard sync
//                           (Osc 1's wrap resets Osc 2) and feedback (Osc 1 phase-mods Osc 2).
//   Lane B (amplitude):     AM, ring mod, or SSB frequency shift of Osc 1.
// The voice sets frequencies / tables / positions on osc1 / osc2, calls beginBlock once per
// block, then next() per sample for the {carrier, modulator} pair, and endBlock afterwards.
namespace mu_wavetable
{

struct XModSettings
{
    int   phaseMode = 1;      // Lane A: 0 = FM (true freq-mod), 1 = PM (default, drone-safe), 2 = TZFM
    float index     = 0.0f;   // 0..1 modulation index (Osc 2 → Osc 1)
    bool  sync      = false;  // hard sync: Osc 1 wrap resets Osc 2 phase
    bool  feedback  = false;  // mutual feedback FM: Osc 1's previous output phase-mods Osc 2
    int   ampMode   = 0;      // Lane B: 0 = AM (carrier kept), 1 = RM (carrier suppressed), 2 = SSB
    float depth     = 0.0f;   // -1..1: centre = off; AM / RM amount; sign flips the modulator phase
    float ssbHz     = 0.0f;   // SSB shift in Hz (bipolar; sign = shift up / down)
};

class XModOscPair
{
public:
    WavetableOscillator osc1, osc2;   // carrier, modulator

    void prepare(double sampleRate) noexcept
    {
        sr = sampleRate > 0 ? sampleRate : 44100.0;
        osc1.prepare(sr);
        osc2.prepare(sr);
        reset();
    }

    void setBank(const WavetableBank* b) noexcept { osc1.setBank(b); osc2.setBank(b); }

    void reset() noexcept
    {
        hilbert.reset();
        lastA  = 0.0f;
        ssbCos = 1.0f; ssbSin = 0.0f;
        indexSm = depthSm = ssbHzSm = 0.0f;
    }

    // Per block: take the settings, work out the smoothing coefficient and (SSB) the per-sample
    // phasor rotation — one cos / sin pair per block instead of per sample.
    void beginBlock(const XModSettings& s, int numSamples) noexcept
    {
        set    = s;
        idxTgt = juce::jlimit(0.0f, 1.0f, s.index);
        depTgt = juce::jlimit(-1.0f, 1.0f, s.depth);
        smCoef = 1.0f - std::exp(-1.0f / (kXModSmoothMs * 0.001f * (float) sr));
        rotC = 1.0f; rotS = 0.0f;
        if (set.ampMode == 2)
        {
            ssbHzSm += (s.ssbHz - ssbHzSm) * juce::jmin(1.0f, smCoef * (float) numSamples);
            const double omega = juce::MathConstants<double>::twoPi * (double) ssbHzSm / sr;
            rotC = (float) std::cos(omega);
            rotS = (float) std::sin(omega);
        }
    }

    struct Out { float carrier, modulator; };

    // One sample: the cross-modulated carrier (Osc 1) and the modulator (Osc 2).
    Out next() noexcept
    {
        indexSm += (idxTgt - indexSm) * smCoef;
        depthSm += (depTgt - depthSm) * smCoef;

        // Modulator (osc2) renders first; feedback FM phase-mods it with osc1's previous output.
        const float b = osc2.render(set.feedback ? kFbScale * lastA : 0.0f);

        // Lane A — carrier (osc1) phase / index bus.
        float a;
        if (set.phaseMode == 1)                       // PM: displace osc1's read phase
        {
            a = osc1.render(indexSm * b * kPmScale);
        }
        else                                          // FM / TZFM: scale osc1's increment
        {
            double incMul = 1.0 + (double) (indexSm * kFmRatio) * b;
            if (set.phaseMode == 0) incMul = juce::jmax(0.0, incMul);   // FM clamps ≥ 0 (no through-zero)
            a = osc1.render(0.0f, incMul);
        }

        // Hard sync: osc1 wrap resets osc2 phase (takes effect next sample).
        if (set.sync && osc1.justWrapped())
            osc2.resetPhase();

        lastA = a;                                    // feedback tap = raw carrier output

        // Lane B — amplitude / multiply bus.
        if (set.ampMode == 2)
        {
            // SSB / frequency shift: the analytic signal (Hilbert) times the running complex
            // phasor, real part kept → one sideband; then advance the phasor one sample.
            const auto q = hilbert.process(a);
            a = q.re * ssbCos - q.im * ssbSin;
            const float nc = ssbCos * rotC - ssbSin * rotS;
            ssbSin         = ssbCos * rotS + ssbSin * rotC;
            ssbCos         = nc;
        }
        else if (depthSm != 0.0f)
        {
            // AM keeps the carrier; RM crossfades dry → ring so the carrier is suppressed at full
            // depth. Depth is bipolar (centre = off; sign flips the modulator phase).
            if (set.ampMode == 0)
            {
                a *= 1.0f + depthSm * b;
            }
            else
            {
                const float k   = std::abs(depthSm);
                const float sgn = depthSm < 0.0f ? -1.0f : 1.0f;
                a = a * (1.0f - k) + sgn * (a * b) * k;
            }
        }
        return { a, b };
    }

    // Renormalise the SSB phasor once per block (counters slow magnitude drift from the
    // recursive rotation without a per-sample sqrt).
    void endBlock() noexcept
    {
        if (set.ampMode != 2) return;
        const float mag = std::sqrt(ssbCos * ssbCos + ssbSin * ssbSin);
        if (mag > 1.0e-6f) { ssbCos /= mag; ssbSin /= mag; }
    }

private:
    // X-Mod index scaling: PM index 1.0 → up to 2 cycles of phase displacement ("DX-style");
    // FM / TZFM index 1.0 → up to an 8× frequency-modulation ratio; feedback a conservative
    // fixed phase depth (feedback FM turns chaotic fast).
    static constexpr float kPmScale = 2.0f;
    static constexpr float kFmRatio = 8.0f;
    static constexpr float kFbScale = 0.3f;
    // One-pole smoothing time for the continuous controls (index / depth / SSB shift), so a
    // knob sweep or a per-block mode change ramps instead of zippering / clicking.
    static constexpr float kXModSmoothMs = 5.0f;

    HilbertTransform hilbert;
    XModSettings     set;
    double sr      = 44100.0;
    float  idxTgt  = 0.0f, depTgt = 0.0f, smCoef = 1.0f;
    float  rotC    = 1.0f, rotS = 0.0f;
    float  lastA   = 0.0f;
    float  ssbCos  = 1.0f, ssbSin = 0.0f;
    float  indexSm = 0.0f, depthSm = 0.0f, ssbHzSm = 0.0f;
};

} // namespace mu_wavetable
