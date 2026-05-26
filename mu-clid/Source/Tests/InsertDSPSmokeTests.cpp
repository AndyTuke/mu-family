// Insert algorithm DSP smoke tests.
//
// Verify two regression-prone properties for all 14 insert algorithms:
//   1. process() runs without crash or NaN/Inf output.
//   2. KarplusStrongInsert output level responds when the engine carries
//      audible energy (the prior insertOutput-trim check is now redundant
//      since Stage 36 removed the per-algorithm gain stage on Karplus).
//
// These are not golden-master tests — exact DSP output is not asserted.
// The goals are: (a) no crash, (b) no NaN/Inf in output buffer, (c) the
// Karplus loop produces non-zero output when fed an impulse.

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include "Audio/FX/Insert/NoneInsert.h"
#include "Audio/FX/Insert/SoftClipInsert.h"
#include "Audio/FX/Insert/HardClipInsert.h"
#include "Audio/FX/Insert/FoldInsert.h"
#include "Audio/FX/Insert/BitcrusherInsert.h"
#include "Audio/FX/Insert/ClipperInsert.h"
#include "Audio/FX/Insert/EqInsert.h"
#include "Audio/FX/Insert/CompressorLimiterInsert.h"
#include "Audio/FX/Insert/RingModInsert.h"
#include "Audio/FX/Insert/TapeSatInsert.h"
#include "Audio/FX/Insert/KarplusStrongInsert.h"
#include "Audio/FX/Insert/VocoderInsert.h"
#include "Audio/VoiceParams.h"
#include "Audio/InsertSlotConfig.h"

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

// Stage 36: helper to set a slot using ACTUAL value (test reads more clearly
// than the normalised 0..1 storage). Routes through actualToNorm for the
// active algorithm's slot range.
static void setSlot(VoiceParams& p, int slot, float actual)
{
    p.insertParam[slot] = mu_ui::actualToNorm(actual, p.insertAlgo, slot);
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
            buf.setSample(0, 0, 1.0f);
            buf.setSample(1, 0, 1.0f);

            VoiceParams p;
            p.insertAlgo = 11;       // Karplus
            setSlot(p, 0, 0.0f);     // Note C
            setSlot(p, 1, 1.0f);     // Octave 1
            setSlot(p, 2, 80.0f);    // Feedback 80%
            setSlot(p, 3, 5000.0f);  // LPF 5 kHz

            float gr = 0.0f;
            k.process(buf, kNs, 2, p, gr);
            expect (!hasNaN(buf, 2, kNs), "Karplus produced NaN/Inf");
        }

        beginTest ("Karplus: high feedback produces non-zero output on impulse");
        {
            KarplusStrongInsert k;
            k.prepare(kSR, kNs);
            juce::AudioBuffer<float> buf(1, kNs);
            buf.clear();
            buf.setSample(0, 0, 0.5f);

            VoiceParams p;
            p.insertAlgo = 11;
            setSlot(p, 0, 0.0f);
            setSlot(p, 1, 2.0f);
            setSlot(p, 2, 90.0f);
            setSlot(p, 3, 10000.0f);

            float gr = 0.0f;
            k.process(buf, kNs, 1, p, gr);
            const float rms = rmsOf(buf, kNs);
            expect (rms > 1e-6f, "Karplus produced no output — feedback may be broken (RMS=" + juce::String(rms) + ")");
        }

        // ── VocoderInsert (mono) ─────────────────────────────────────────────
        beginTest ("Vocoder mono: no crash, no NaN on impulse");
        {
            VocoderInsert v(false);
            v.prepare(kSR, kNs);

            juce::AudioBuffer<float> buf(2, kNs);
            buf.clear();
            buf.setSample(0, 0, 1.0f);
            buf.setSample(1, 0, 1.0f);

            VoiceParams p;
            p.insertAlgo = 12;
            setSlot(p, 0, 0.0f);   // Wave: Saw
            setSlot(p, 1, 0.0f);   // Unison index 0 (1 voice)
            setSlot(p, 2, 1.0f);   // Octave 1
            setSlot(p, 3, 3.0f);   // Note D (idx 3 → "D")

            float gr = 0.0f;
            v.process(buf, kNs, 2, p, gr);
            expect (!hasNaN(buf, 2, kNs), "Vocoder produced NaN/Inf");
        }

        // ── VocoderInsert (stereo) ────────────────────────────────────────────
        beginTest ("VocoderSt: no crash, no NaN on impulse");
        {
            VocoderInsert v(true);
            v.prepare(kSR, kNs);

            juce::AudioBuffer<float> buf(2, kNs);
            buf.clear();
            buf.setSample(0, 0, 1.0f);
            buf.setSample(1, 0, 1.0f);

            VoiceParams p;
            p.insertAlgo = 13;
            setSlot(p, 0, 0.0f);
            setSlot(p, 1, 0.0f);
            setSlot(p, 2, 1.0f);
            setSlot(p, 3, 3.0f);

            float gr = 0.0f;
            v.process(buf, kNs, 2, p, gr);
            expect (!hasNaN(buf, 2, kNs), "VocoderSt produced NaN/Inf");
        }
    }
};

static InsertDSPSmokeTest insertDSPSmokeTest;

// ── All-algorithm smoke test ──────────────────────────────────────────────────
// Verifies every insert algorithm (0-13) runs without crash or NaN on a
// stereo impulse, using each algorithm's per-slot configuration via the
// generic insertParam[N] storage.
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

    // Build a VoiceParams configured for `algo` with sensible per-slot values,
    // storing them as normalised 0..1 via the per-algo config table.
    VoiceParams makeParams(int algo, std::initializer_list<float> actualSlotValues)
    {
        VoiceParams p;
        p.insertAlgo = algo;
        int s = 0;
        for (float v : actualSlotValues)
        {
            if (s >= mu_ui::kInsertSlotCount) break;
            p.insertParam[s] = mu_ui::actualToNorm(v, algo, s);
            ++s;
        }
        return p;
    }

    void runTest() override
    {
        beginTest ("algo 0: NoneInsert — no crash, no NaN");
        { NoneInsert a; smokeAlgo(a, "NoneInsert", makeParams(0, {})); }

        beginTest ("algo 1: SoftClipInsert — no crash, no NaN");
        { SoftClipInsert a; smokeAlgo(a, "SoftClipInsert", makeParams(1, { 30.0f, 0.0f, 0.0f, 20000.0f })); }

        beginTest ("algo 2: HardClipInsert — no crash, no NaN");
        { HardClipInsert a; smokeAlgo(a, "HardClipInsert", makeParams(2, { 30.0f, 0.0f, 0.0f, 20000.0f })); }

        beginTest ("algo 3: FoldInsert — no crash, no NaN");
        { FoldInsert a; smokeAlgo(a, "FoldInsert", makeParams(3, { 30.0f, 0.0f, 0.0f, 20000.0f })); }

        beginTest ("algo 4: BitcrusherInsert — no crash, no NaN");
        { BitcrusherInsert a; smokeAlgo(a, "BitcrusherInsert", makeParams(4, { 8.0f, 8000.0f, 20.0f, 20000.0f })); }

        beginTest ("algo 5: ClipperInsert — no crash, no NaN");
        { ClipperInsert a; smokeAlgo(a, "ClipperInsert", makeParams(5, { 30.0f, 0.0f, 0.0f, 20000.0f })); }

        beginTest ("algo 6: EqInsert — no crash, no NaN");
        {
            EqInsert a;
            smokeAlgo(a, "EqInsert", makeParams(6, { 0.0f, 6.0f, 1000.0f, 0.0f }));
        }

        beginTest ("algo 7: CompressorInsert — no crash, no NaN");
        {
            VoiceParams cmp = makeParams(7, { 50.0f, 0.0f, 5.0f, 100.0f });
            CompressorLimiterInsert a;
            smokeAlgo(a, "CompressorInsert", cmp);
        }

        beginTest ("algo 8: LimiterInsert — no crash, no NaN");
        {
            VoiceParams lim = makeParams(8, { 50.0f, 0.0f, 1.0f, 50.0f });
            CompressorLimiterInsert a;
            smokeAlgo(a, "LimiterInsert", lim);
        }

        beginTest ("algo 9: RingModInsert — no crash, no NaN");
        {
            VoiceParams rm = makeParams(9, { 100.0f, 0.0f, 0.0f, 200.0f });
            RingModInsert a;
            smokeAlgo(a, "RingModInsert", rm);
        }

        beginTest ("algo 10: TapeSatInsert — no crash, no NaN");
        { TapeSatInsert a; smokeAlgo(a, "TapeSatInsert", makeParams(10, { 30.0f, 0.0f, 0.0f, 5000.0f })); }
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
