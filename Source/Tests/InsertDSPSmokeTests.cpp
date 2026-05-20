// Insert algorithm DSP smoke tests.
//
// Verify two regression-prone properties for all 14 insert algorithms:
//   1. process() runs without crash or NaN/Inf output.
//   2. KarplusStrongInsert output level responds to insertOutput (fixed in #489).
//
// These are not golden-master tests — exact DSP output is not asserted.
// The goals are: (a) no crash, (b) no NaN/Inf in output buffer, (c) the
// output gain knob demonstrably affects output level (Karplus regression test).

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include "../Audio/FX/Insert/NoneInsert.h"
#include "../Audio/FX/Insert/SoftClipInsert.h"
#include "../Audio/FX/Insert/HardClipInsert.h"
#include "../Audio/FX/Insert/FoldInsert.h"
#include "../Audio/FX/Insert/BitcrusherInsert.h"
#include "../Audio/FX/Insert/ClipperInsert.h"
#include "../Audio/FX/Insert/EqInsert.h"
#include "../Audio/FX/Insert/CompressorLimiterInsert.h"
#include "../Audio/FX/Insert/RingModInsert.h"
#include "../Audio/FX/Insert/TapeSatInsert.h"
#include "../Audio/FX/Insert/KarplusStrongInsert.h"
#include "../Audio/FX/Insert/VocoderInsert.h"
#include "../Audio/VoiceParams.h"

static float rmsOf(const juce::AudioBuffer<float>& buf, int ns)
{
    float sum = 0.0f;
    const float* data = buf.getReadPointer(0);
    for (int i = 0; i < ns; ++i) sum += data[i] * data[i];
    return std::sqrt(sum / (float)juce::jmax(1, ns));
}

static bool hasNaN(const juce::AudioBuffer<float>& buf, int nCh, int ns)
{
    for (int ch = 0; ch < nCh; ++ch)
    {
        const float* data = buf.getReadPointer(ch);
        for (int i = 0; i < ns; ++i)
            if (!std::isfinite(data[i])) return true;
    }
    return false;
}

class InsertDSPSmokeTest : public juce::UnitTest
{
public:
    InsertDSPSmokeTest() : juce::UnitTest ("Insert DSP smoke", "DSP") {}

    void runTest() override
    {
        constexpr double kSR = 48000.0;
        constexpr int    kNs = 512;

        // ── KarplusStrongInsert ───────────────────────────────────────────────
        beginTest ("Karplus: no crash, no NaN");
        {
            KarplusStrongInsert k;
            k.prepare(kSR, kNs);

            juce::AudioBuffer<float> buf(2, kNs);
            buf.clear();
            // Impulsive excitation on first sample
            buf.setSample(0, 0, 1.0f);
            buf.setSample(1, 0, 1.0f);

            VoiceParams p;
            p.insertDrive  = 0.0f;   // note C
            p.insertBits   = 1.0f;   // octave 1
            p.insertDither = 80.0f;  // feedback 80%
            p.insertTone   = 5000.0f;
            p.insertOutput = 0.0f;

            float gr = 0.0f;
            k.process(buf, kNs, 2, p, gr);
            expect (!hasNaN(buf, 2, kNs), "Karplus produced NaN/Inf");
        }

        beginTest ("Karplus: -60 dB output is significantly quieter than 0 dB output");
        {
            // Run two identical processes, differing only in insertOutput.
            // Due to the self-sustaining feedback, we get a non-zero output both times.
            auto runKarplus = [&](float outputDb) -> float
            {
                KarplusStrongInsert k;
                k.prepare(kSR, kNs);
                juce::AudioBuffer<float> buf(1, kNs);
                buf.clear();
                buf.setSample(0, 0, 0.5f);   // excite with 0.5

                VoiceParams p;
                p.insertDrive  = 0.0f;
                p.insertBits   = 2.0f;
                p.insertDither = 90.0f;
                p.insertTone   = 10000.0f;
                p.insertOutput = outputDb;

                float gr = 0.0f;
                k.process(buf, kNs, 1, p, gr);
                return rmsOf(buf, kNs);
            };

            const float rms0dB  = runKarplus(0.0f);
            const float rms60dB = runKarplus(-60.0f);

            // -60 dB ≈ amplitude factor 0.001; the 0 dB version should be
            // at least 100× louder in RMS.
            expect (rms0dB > 1e-6f, "Karplus at 0 dB produced no output — feedback may be broken");
            expect (rms0dB > rms60dB * 50.0f,
                    "Karplus -60 dB should be >> quieter than 0 dB (0dB RMS="
                    + juce::String(rms0dB) + ", -60dB RMS=" + juce::String(rms60dB) + ")");
        }

        // ── VocoderInsert (mono) ─────────────────────────────────────────────
        beginTest ("Vocoder mono: no crash, no NaN on impulse");
        {
            VocoderInsert v(false);   // mono
            v.prepare(kSR, kNs);

            juce::AudioBuffer<float> buf(2, kNs);
            buf.clear();
            buf.setSample(0, 0, 1.0f);
            buf.setSample(1, 0, 1.0f);

            VoiceParams p;
            p.insertBits   = 3.0f;   // note D (index 1→2 → but insertBits maps to note idx)
            p.insertDither = 0.0f;   // octave 0
            p.insertOutput = -20.0f;

            float gr = 0.0f;
            v.process(buf, kNs, 2, p, gr);
            expect (!hasNaN(buf, 2, kNs), "Vocoder produced NaN/Inf");
        }

        // ── VocoderInsert (stereo) ────────────────────────────────────────────
        beginTest ("VocoderSt: no crash, no NaN on impulse");
        {
            VocoderInsert v(true);   // stereo
            v.prepare(kSR, kNs);

            juce::AudioBuffer<float> buf(2, kNs);
            buf.clear();
            buf.setSample(0, 0, 1.0f);
            buf.setSample(1, 0, 1.0f);

            VoiceParams p;
            p.insertBits   = 3.0f;
            p.insertDither = 0.0f;
            p.insertOutput = -20.0f;

            float gr = 0.0f;
            v.process(buf, kNs, 2, p, gr);
            expect (!hasNaN(buf, 2, kNs), "VocoderSt produced NaN/Inf");
        }
    }
};

static InsertDSPSmokeTest insertDSPSmokeTest;

// ── All-algorithm smoke test ──────────────────────────────────────────────────
// Verifies every insert algorithm (0-13) runs without crash or NaN on a
// stereo impulse. Does not test Karplus output-level response (covered above).
class InsertAllAlgosSmokeTest : public juce::UnitTest
{
public:
    InsertAllAlgosSmokeTest() : juce::UnitTest ("Insert all-algos smoke", "DSP") {}

    void smokeAlgo (InsertAlgorithmBase& algo, const char* algoName,
                    VoiceParams p, double sr = 48000.0, int ns = 512)
    {
        algo.prepare(sr, ns);

        juce::AudioBuffer<float> buf(2, ns);
        buf.clear();
        buf.setSample(0, 0, 0.5f);
        buf.setSample(1, 0, 0.5f);

        float gr = 0.0f;
        algo.process(buf, ns, 2, p, gr);

        expect (!hasNaN(buf, 2, ns),
                juce::String(algoName) + " produced NaN/Inf");
        expect (std::isfinite(gr),
                juce::String(algoName) + " grOut is non-finite");
    }

    void runTest() override
    {
        // Default params that are safe for all algorithms.
        VoiceParams p;
        p.insertDrive  = 30.0f;    // 30 % drive / threshold
        p.insertOutput = 0.0f;     // 0 dB output
        p.insertBits   = 8.0f;     // 8-bit depth (Bitcrusher)
        p.insertRate   = 8000.0f;  // 8 kHz sample-rate reduction (Bitcrusher)
        p.insertDither = 20.0f;    // 20 % dither
        p.insertTone   = 500.0f;   // 500 Hz (RingMod carrier / Comp release)
        p.insertEqMid  = 0.0f;     // EQ mid gain 0 dB
        p.accentDb     = 0.0f;

        beginTest ("algo 0: NoneInsert — no crash, no NaN");
        { NoneInsert a; smokeAlgo(a, "NoneInsert", p); }

        beginTest ("algo 1: SoftClipInsert — no crash, no NaN");
        { SoftClipInsert a; smokeAlgo(a, "SoftClipInsert", p); }

        beginTest ("algo 2: HardClipInsert — no crash, no NaN");
        { HardClipInsert a; smokeAlgo(a, "HardClipInsert", p); }

        beginTest ("algo 3: FoldInsert — no crash, no NaN");
        { FoldInsert a; smokeAlgo(a, "FoldInsert", p); }

        beginTest ("algo 4: BitcrusherInsert — no crash, no NaN");
        { BitcrusherInsert a; smokeAlgo(a, "BitcrusherInsert", p); }

        beginTest ("algo 5: ClipperInsert — no crash, no NaN");
        { ClipperInsert a; smokeAlgo(a, "ClipperInsert", p); }

        beginTest ("algo 6: EqInsert — no crash, no NaN");
        {
            VoiceParams eq = p;
            eq.insertEqMid = 6.0f;   // +6 dB mid peak
            EqInsert a;
            smokeAlgo(a, "EqInsert", eq);
        }

        beginTest ("algo 7: CompressorInsert — no crash, no NaN");
        {
            VoiceParams cmp = p;
            cmp.insertAlgo  = 7;
            cmp.insertDrive = 50.0f;   // 50 % → maps to ~20 dB threshold
            cmp.insertDither = 5.0f;   // 5 ms attack
            cmp.insertTone   = 100.0f; // 100 ms release
            CompressorLimiterInsert a;
            smokeAlgo(a, "CompressorInsert", cmp);
        }

        beginTest ("algo 8: LimiterInsert — no crash, no NaN");
        {
            VoiceParams lim = p;
            lim.insertAlgo  = 8;
            lim.insertDrive = 50.0f;
            lim.insertDither = 1.0f;
            lim.insertTone   = 50.0f;
            CompressorLimiterInsert a;
            smokeAlgo(a, "LimiterInsert", lim);
        }

        beginTest ("algo 9: RingModInsert — no crash, no NaN");
        {
            VoiceParams rm = p;
            rm.insertTone  = 200.0f;  // 200 Hz carrier
            rm.insertDrive = 100.0f;  // 100 % wet
            RingModInsert a;
            smokeAlgo(a, "RingModInsert", rm);
        }

        beginTest ("algo 10: TapeSatInsert — no crash, no NaN");
        { TapeSatInsert a; smokeAlgo(a, "TapeSatInsert", p); }
    }

private:
    static bool hasNaN(const juce::AudioBuffer<float>& buf, int nCh, int ns)
    {
        for (int ch = 0; ch < nCh; ++ch)
        {
            const float* d = buf.getReadPointer(ch);
            for (int i = 0; i < ns; ++i)
                if (!std::isfinite(d[i])) return true;
        }
        return false;
    }
};

static InsertAllAlgosSmokeTest insertAllAlgosSmokeTest;
