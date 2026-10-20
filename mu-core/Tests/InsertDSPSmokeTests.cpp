// Insert algorithm DSP smoke tests.
//
// Verify two regression-prone properties for all 14 insert algorithms:
//   1. process() runs without crash or NaN/Inf output.
//   2. KarplusStrongInsert output level responds when the engine carries
//      audible energy (the prior insertOutput-trim check is now redundant
//      since Stage 36 removed the per-algorithm gain stage on Karplus).
//
// These are not golden-master tests - exact DSP output is not asserted.
// The goals are: (a) no crash, (b) no NaN/Inf in output buffer, (c) the
// Karplus loop produces non-zero output when fed an impulse.

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>
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
#include "Audio/FX/Insert/OverdriveInsert.h"
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
            expect (rms > 1e-6f, "Karplus produced no output - feedback may be broken (RMS=" + juce::String(rms) + ")");
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
            setSlot(p, 3, 3.0f);   // Note D (idx 3 -> "D")

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

        // ── Noise carriers are seeded per slot ───────────────────────────────
        beginTest ("Vocoder white-noise carrier: slots 0 and 1 decorrelate, equal indices match, reset equals fresh");
        {
            constexpr int kLen = 8192;
            VoiceParams p;
            p.insertAlgo = 12;
            setSlot(p, 0, 2.0f);   // Wave: White noise

            // Renders kLen samples of a fixed two-sine modulator through a vocoder serving `slot`.
            auto render = [&](VocoderInsert& v, int slot, bool resetFirst)
            {
                v.prepare(kSR, kLen);
                v.setSlotIndex(slot);
                if (resetFirst) { juce::AudioBuffer<float> junk(2, 256); junk.clear(); float g = 0.0f; v.process(junk, 256, 2, p, g); v.reset(); }
                juce::AudioBuffer<float> buf(2, kLen);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < kLen; ++i)
                        buf.setSample(ch, i, 0.4f * std::sin(0.031f * (float) i) + 0.3f * std::sin(0.173f * (float) i));
                float gr = 0.0f;
                v.process(buf, kLen, 2, p, gr);
                std::vector<float> out((size_t) kLen);
                for (int i = 0; i < kLen; ++i) out[(size_t) i] = buf.getSample(0, i);
                return out;
            };

            VocoderInsert a(false), b(false), c(false), d(false);
            const auto s0  = render(a, 0, false);
            const auto s1  = render(b, 1, false);
            const auto s0b = render(c, 0, false);
            const auto s0r = render(d, 0, true);

            double dot = 0.0, na = 0.0, nb = 0.0;
            for (int i = 0; i < kLen; ++i)
            {
                dot += (double) s0[(size_t) i] * s1[(size_t) i];
                na  += (double) s0[(size_t) i] * s0[(size_t) i];
                nb  += (double) s1[(size_t) i] * s1[(size_t) i];
            }
            const double corr = dot / std::sqrt(juce::jmax(1.0e-30, na * nb));
            expect (na > 1.0e-6 && nb > 1.0e-6, "the vocoder rendered silence");
            expect (std::abs(corr) < 0.5, "slots 0 and 1 produce correlated noise carriers (corr=" + juce::String(corr) + ")");
            expect (s0 == s0b, "two slots of the same index must render identically");
            expect (s0 == s0r, "a reset slot must render like a fresh slot of the same index");
        }

        // ── Bitcrusher dither is seeded per slot ─────────────────────────────
        beginTest ("Bitcrusher dither: slots 0 and 1 decorrelate, equal indices match, reset equals fresh");
        {
            constexpr int kLen = 8192;
            VoiceParams p;
            p.insertAlgo = 4;
            setSlot(p, 0, 2.0f);     // 2 bits: a step of 0.5
            setSlot(p, 1, 48000.0f); // full rate: every sample is quantised
            setSlot(p, 2, 100.0f);   // full dither

            // A DC input on the quantiser's decision threshold, so the dither alone picks each output level.
            auto render = [&](BitcrusherInsert& b, int slot, bool resetFirst)
            {
                b.prepare(kSR, kLen);
                b.setSlotIndex(slot);
                if (resetFirst) { juce::AudioBuffer<float> junk(2, 256); junk.clear(); float g = 0.0f; b.process(junk, 256, 2, p, g); b.reset(); }
                juce::AudioBuffer<float> buf(2, kLen);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < kLen; ++i)
                        buf.setSample(ch, i, 0.25f);
                float gr = 0.0f;
                b.process(buf, kLen, 2, p, gr);
                std::vector<float> out((size_t) kLen);
                for (int i = 0; i < kLen; ++i) out[(size_t) i] = buf.getSample(0, i);
                return out;
            };

            BitcrusherInsert a, b, c, d;
            const auto s0  = render(a, 0, false);
            const auto s1  = render(b, 1, false);
            const auto s0b = render(c, 0, false);
            const auto s0r = render(d, 0, true);

            // Correlation of the zero-mean parts: the shared DC level would otherwise read as similarity.
            double m0 = 0.0, m1 = 0.0;
            for (int i = 0; i < kLen; ++i) { m0 += s0[(size_t) i]; m1 += s1[(size_t) i]; }
            m0 /= kLen; m1 /= kLen;
            double dot = 0.0, na = 0.0, nb = 0.0;
            for (int i = 0; i < kLen; ++i)
            {
                const double x = s0[(size_t) i] - m0, y = s1[(size_t) i] - m1;
                dot += x * y; na += x * x; nb += y * y;
            }
            const double corr = dot / std::sqrt(juce::jmax(1.0e-30, na * nb));
            expect (na > 1.0e-6 && nb > 1.0e-6, "the dither did not move the output");
            expect (std::abs(corr) < 0.2, "slots 0 and 1 produce correlated dither (corr=" + juce::String(corr) + ")");
            expect (s0 == s0b, "two slots of the same index must render identically");
            expect (s0 == s0r, "a reset slot must render like a fresh slot of the same index");
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
        beginTest ("algo 0: NoneInsert - no crash, no NaN");
        { NoneInsert a; smokeAlgo(a, "NoneInsert", makeParams(0, {})); }

        beginTest ("algo 1: SoftClipInsert - no crash, no NaN");
        { SoftClipInsert a; smokeAlgo(a, "SoftClipInsert", makeParams(1, { 30.0f, 0.0f, 0.0f, 20000.0f })); }

        beginTest ("algo 2: HardClipInsert - no crash, no NaN");
        { HardClipInsert a; smokeAlgo(a, "HardClipInsert", makeParams(2, { 30.0f, 0.0f, 0.0f, 20000.0f })); }

        beginTest ("algo 3: FoldInsert - no crash, no NaN");
        { FoldInsert a; smokeAlgo(a, "FoldInsert", makeParams(3, { 30.0f, 0.0f, 0.0f, 20000.0f })); }

        beginTest ("algo 4: BitcrusherInsert - no crash, no NaN");
        { BitcrusherInsert a; smokeAlgo(a, "BitcrusherInsert", makeParams(4, { 8.0f, 8000.0f, 20.0f, 20000.0f })); }

        beginTest ("algo 5: ClipperInsert - no crash, no NaN");
        { ClipperInsert a; smokeAlgo(a, "ClipperInsert", makeParams(5, { 30.0f, 0.0f, 0.0f, 20000.0f })); }

        beginTest ("algo 6: EqInsert - no crash, no NaN");
        {
            EqInsert a;
            smokeAlgo(a, "EqInsert", makeParams(6, { 0.0f, 6.0f, 1000.0f, 0.0f }));
        }

        beginTest ("algo 7: CompressorInsert - no crash, no NaN");
        {
            VoiceParams cmp = makeParams(7, { 50.0f, 0.0f, 5.0f, 100.0f });
            CompressorLimiterInsert a;
            smokeAlgo(a, "CompressorInsert", cmp);
        }

        beginTest ("algo 8: LimiterInsert - no crash, no NaN");
        {
            VoiceParams lim = makeParams(8, { 50.0f, 0.0f, 1.0f, 50.0f });
            CompressorLimiterInsert a;
            smokeAlgo(a, "LimiterInsert", lim);
        }

        beginTest ("algo 9: RingModInsert - no crash, no NaN");
        {
            VoiceParams rm = makeParams(9, { 100.0f, 0.0f, 0.0f, 200.0f });
            RingModInsert a;
            smokeAlgo(a, "RingModInsert", rm);
        }

        beginTest ("algo 10: TapeSatInsert - no crash, no NaN");
        { TapeSatInsert a; smokeAlgo(a, "TapeSatInsert", makeParams(10, { 30.0f, 0.0f, 0.0f, 5000.0f })); }

        beginTest ("algo 14: OverdriveInsert - no crash, no NaN");
        { OverdriveInsert a; smokeAlgo(a, "OverdriveInsert", makeParams(14, { 60.0f, 0.0f, 50.0f, 5000.0f })); }

        beginTest ("algo 14: OverdriveInsert - silence stays silent at any bias, and Bias adds even harmonics");
        {
            // Magnitude of `harmonic` x the test tone in a settled one-second render (Goertzel).
            auto harmonicLevel = [&](float biasPercent, int harmonic, bool silentInput)
            {
                constexpr double sr = 48000.0;
                constexpr int n = 48000;
                const double f0 = 1000.0;
                OverdriveInsert a;
                a.prepare(sr, n);
                juce::AudioBuffer<float> buf(2, n);
                for (int i = 0; i < n; ++i)
                    for (int c = 0; c < 2; ++c)
                        buf.setSample(c, i, silentInput ? 0.0f : 0.25f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * f0 * i / sr));
                float gr = 0.0f;
                a.process(buf, n, 2, makeParams(14, { 50.0f, 0.0f, biasPercent, 20000.0f }), gr);
                if (silentInput)
                {
                    float peak = 0.0f;
                    for (int i = 0; i < n; ++i) peak = juce::jmax(peak, std::abs(buf.getSample(0, i)));
                    return (double) peak;
                }
                const double w = 2.0 * juce::MathConstants<double>::pi * f0 * harmonic / sr;
                double re = 0.0, im = 0.0;
                for (int i = n / 2; i < n; ++i)   // the second half: past the gain and DC-block settling
                {
                    re += buf.getSample(0, i) * std::cos(w * i);
                    im += buf.getSample(0, i) * std::sin(w * i);
                }
                return std::sqrt(re * re + im * im) / (n / 2);
            };
            for (const float bias : { 0.0f, 50.0f, 100.0f })
                expect(harmonicLevel(bias, 1, true) < 1.0e-3, "silence in gives silence out at bias " + juce::String(bias));
            // A loud low note at full drive: the clipped top is flat, not noisy (the ADAA large-|x| form's rounding error).
            {
                constexpr double sr = 48000.0;
                constexpr int n = 24000;
                OverdriveInsert a;
                a.prepare(sr, n);
                juce::AudioBuffer<float> buf(2, n);
                for (int i = 0; i < n; ++i)
                    for (int c = 0; c < 2; ++c)
                        buf.setSample(c, i, (float) std::sin(2.0 * juce::MathConstants<double>::pi * 50.0 * i / sr));
                float gr = 0.0f;
                a.process(buf, n, 2, makeParams(14, { 100.0f, 0.0f, 0.0f, 20000.0f }), gr);
                float worst = 0.0f;   // the largest sample-to-sample step inside the flat top
                for (int i = 4990; i < 5090; ++i)   // around the crest, a quarter of the way into the fifth cycle
                    worst = juce::jmax(worst, std::abs(buf.getSample(0, i) - buf.getSample(0, i - 1)));
                expect(worst < 0.05f, "the flat top is smooth (largest step " + juce::String(worst) + ")");
            }
            const double second0 = harmonicLevel(0.0f, 2, false), second100 = harmonicLevel(100.0f, 2, false);
            expect(second0 < 1.0e-3, "no second harmonic from the symmetric curve");
            expect(second100 > 20.0 * juce::jmax(second0, 1.0e-6), "Bias 100 % puts a clear second harmonic in");
        }
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

// Inserts keep per-channel state for two channels. A buffer of more than two channels must leave channels 0
// and 1 exactly as a stereo buffer would, rather than the extra channels sharing (and corrupting) their state.
class InsertMultiChannelStateTest : public juce::UnitTest
{
public:
    InsertMultiChannelStateTest() : juce::UnitTest ("Insert state with more than two channels", "DSP") {}

    void runTest() override
    {
        constexpr double kSR = 48000.0;
        constexpr int    kNs = 512;

        for (const int algo : { 7, 8, 10 })   // Compressor, Limiter, TapeSat
        {
            beginTest ("algo " + juce::String(algo) + ": channels 2 and 3 do not disturb channels 0 and 1");

            VoiceParams p;
            p.insertAlgo = algo;
            if (algo == 10)
            {
                p.insertParam[0] = mu_ui::actualToNorm(60.0f,   algo, 0);   // Drive
                p.insertParam[3] = mu_ui::actualToNorm(4000.0f, algo, 3);   // Tone
            }
            else
            {
                p.insertParam[0] = mu_ui::actualToNorm(80.0f,   algo, 0);   // Threshold
                p.insertParam[2] = mu_ui::actualToNorm(5.0f,    algo, 2);   // Attack
                p.insertParam[3] = mu_ui::actualToNorm(100.0f,  algo, 3);   // Release
            }

            // Quiet sines on channels 0 and 1; a hard loud signal on the extra channels.
            auto fill = [&](juce::AudioBuffer<float>& b)
            {
                for (int ch = 0; ch < b.getNumChannels(); ++ch)
                    for (int i = 0; i < kNs; ++i)
                        b.setSample(ch, i, ch < 2 ? 0.2f * std::sin(0.05f * (float) i * (float) (ch + 1))
                                                  : (((i / 8) & 1) ? 0.95f : -0.95f));
            };
            auto run = [&](juce::AudioBuffer<float>& b)
            {
                std::unique_ptr<InsertAlgorithmBase> ins;
                if (algo == 10) ins = std::make_unique<TapeSatInsert>();
                else            ins = std::make_unique<CompressorLimiterInsert>();
                ins->prepare(kSR, kNs);
                float gr = 0.0f;
                for (int block = 0; block < 4; ++block)   // several blocks so the carried state matters
                {
                    fill(b);
                    ins->process(b, kNs, b.getNumChannels(), p, gr);
                }
            };

            juce::AudioBuffer<float> stereo(2, kNs), quad(4, kNs);
            run(stereo);
            run(quad);
            bool same = true;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < kNs; ++i)
                    same = same && quad.getSample(ch, i) == stereo.getSample(ch, i);
            expect(same, "channels 0 and 1 differ from the stereo result");
        }
    }
};

static InsertMultiChannelStateTest insertMultiChannelStateTest;
