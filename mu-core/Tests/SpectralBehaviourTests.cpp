// Behavioural and spectral DSP tests: what the audio sounds like, not that it ran.
//
// Each suite pins a defect that a "no crash, no NaN" smoke test cannot see:
//   - Fold / SoftClip / HardClip level across Drive, and the closed-form fold itself
//   - Notch12 depth and Bp12 passband gain across resonance
//   - Vocoder: the output follows the input's spectrum, and stays in range
//   - X-Mod SSB: the Hilbert transformer's image rejection and quadrature balance
//   - Karplus-Strong: the pitch stays on the note when the loop low-pass is lowered
//
// Every render is short and deterministic (fixed seeds, fixed settings). The thresholds were derived by
// simulation in float64 (not measured in C++); each one is set with headroom and says so where it is used.

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "Audio/FX/Insert/SoftClipInsert.h"
#include "Audio/FX/Insert/HardClipInsert.h"
#include "Audio/FX/Insert/FoldInsert.h"
#include "Audio/FX/Insert/KarplusStrongInsert.h"
#include "Audio/FX/Insert/VocoderInsert.h"
#include "Audio/Filters/Bp12Filter.h"
#include "Audio/Filters/Notch12Filter.h"
#include "Audio/Wavetable/HilbertTransform.h"
#include "Audio/VoiceParams.h"
#include "Audio/InsertSlotConfig.h"

namespace
{
    constexpr double kTwoPi = 6.283185307179586476925286766559;

    // Sets insert slot `slot` of `p` from an actual (not normalised) value for the algorithm in p.insertAlgo.
    void setSlotActual(VoiceParams& p, int slot, float actual)
    {
        p.insertParam[slot] = mu_ui::actualToNorm(actual, p.insertAlgo, slot);
    }

    // Renders `numSamples` of a mono-in-both-channels sine through `algo`, in blocks of `block`, and returns channel 0.
    std::vector<float> renderSine(InsertAlgorithmBase& algo, const VoiceParams& p, double sr,
                                  double freq, float amp, int numSamples, int block)
    {
        std::vector<float> out((size_t) numSamples);
        juce::AudioBuffer<float> buf(2, block);
        float gr = 0.0f;
        for (int start = 0; start < numSamples; start += block)
        {
            const int n = juce::jmin(block, numSamples - start);
            // Phase-continuous input across blocks: the sample index is global.
            for (int i = 0; i < n; ++i)
            {
                const float s = amp * (float) std::sin(kTwoPi * freq * (double) (start + i) / sr);
                buf.setSample(0, i, s);
                buf.setSample(1, i, s);
            }
            algo.process(buf, n, 2, p, gr);
            for (int i = 0; i < n; ++i)
                out[(size_t) (start + i)] = buf.getSample(0, i);
        }
        return out;
    }

    double rmsOf(const std::vector<float>& x, int from, int to)
    {
        double sum = 0.0;
        for (int i = from; i < to; ++i) sum += (double) x[(size_t) i] * (double) x[(size_t) i];
        return std::sqrt(sum / (double) juce::jmax(1, to - from));
    }

    double toDb(double ratio) { return 20.0 * std::log10(juce::jmax(1.0e-12, ratio)); }

    // Power at `freqHz` of x[start, start + len): one Hann-windowed DFT bin. The window keeps the leakage of
    // a strong tone away from a tone hundreds of Hz off, which is what these measurements compare.
    double tonePower(const std::vector<float>& x, int start, int len, double freqHz, double sr)
    {
        const double w = kTwoPi * freqHz / sr;
        double re = 0.0, im = 0.0;
        for (int n = 0; n < len; ++n)
        {
            const double win = 0.5 - 0.5 * std::cos(kTwoPi * (double) n / (double) (len - 1));
            const double ph  = w * (double) (start + n);
            const double v   = win * (double) x[(size_t) (start + n)];
            re += v * std::cos(ph);
            im -= v * std::sin(ph);
        }
        return re * re + im * im;
    }

    // Mean tonePower over `bins` evenly spaced frequencies in [loHz, hiHz]: the energy in a band, per bin.
    double bandPower(const std::vector<float>& x, int start, int len, double loHz, double hiHz, int bins, double sr)
    {
        double sum = 0.0;
        for (int b = 0; b < bins; ++b)
            sum += tonePower(x, start, len, loHz + (hiHz - loHz) * (double) b / (double) (bins - 1), sr);
        return sum / (double) bins;
    }
}

// ── Fold / SoftClip / HardClip ──────────────────────────────────────────────────────────────────────
class DriveInsertLevelTest : public juce::UnitTest
{
public:
    DriveInsertLevelTest() : juce::UnitTest("Drive inserts: Fold closed form and level across Drive", "DSP") {}

    // RMS (dB) of a 0.5-amplitude 440 Hz sine through `algo` at `drive`, measured after the 15 ms gain ramps settle,
    // and the fraction of output samples that are exactly zero.
    void measure(InsertAlgorithmBase& algo, int algoId, float drive, double& rmsDb, double& zeroFraction)
    {
        constexpr double kSr = 48000.0;
        constexpr int kLen = 9600;   // 200 ms; the second half is measured
        algo.prepare(kSr, 512);
        VoiceParams p;
        p.insertAlgo = algoId;
        setSlotActual(p, 0, drive);
        setSlotActual(p, 1, 0.0f);   // Output knob at its maximum, 0 dB
        const auto y = renderSine(algo, p, kSr, 440.0, 0.5f, kLen, 512);
        rmsDb = toDb(rmsOf(y, kLen / 2, kLen));
        int zeros = 0;
        for (int i = kLen / 2; i < kLen; ++i) zeros += (y[(size_t) i] == 0.0f) ? 1 : 0;
        zeroFraction = (double) zeros / (double) (kLen / 2);
    }

    void runTest() override
    {
        beginTest("Fold: the closed form equals the period-4 triangle fold");
        {
            // Drive 0 is a pre-gain of exactly 1 and the Output knob at 0 dB is a gain of 1, so the output is the fold itself.
            struct Point { float in, expected; };
            const Point points[] = { { 0.5f, 0.5f }, { 1.0f, 1.0f }, { 1.5f, 0.5f }, { 3.0f, -1.0f },
                                     { -1.0f, -1.0f }, { -1.5f, -0.5f }, { -3.0f, 1.0f }, { 5.0f, 1.0f }, { -5.0f, -1.0f } };
            for (const auto& pt : points)
            {
                FoldInsert fold;
                fold.prepare(48000.0, 512);
                VoiceParams p;
                p.insertAlgo = 3;
                setSlotActual(p, 0, 0.0f);
                setSlotActual(p, 1, 0.0f);
                juce::AudioBuffer<float> buf(2, 1);
                buf.setSample(0, 0, pt.in);
                buf.setSample(1, 0, pt.in);
                float gr = 0.0f;
                fold.process(buf, 1, 2, p, gr);
                expectWithinAbsoluteError(buf.getSample(0, 0), pt.expected, 1.0e-5f,
                                          "fold(" + juce::String(pt.in) + ")");
                expectWithinAbsoluteError(buf.getSample(1, 0), pt.expected, 1.0e-5f,
                                          "fold(" + juce::String(pt.in) + ") right channel");
            }
        }

        beginTest("Fold: the level holds up and the output is not silent as Drive rises (Drive 25 to 100)");
        {
            // A folded signal stays inside +/-1 whatever the drive, so the RMS should sit in a narrow band. The
            // old code clamped to +/-4 before folding (fold(+/-4) = 0), so 41..95 % of the samples were exactly 0.
            // The +/-6 dB band and the 5 % bound are the simulated figures with headroom.
            double lo = 1.0e9, hi = -1.0e9;
            for (const float drive : { 25.0f, 50.0f, 75.0f, 100.0f })
            {
                FoldInsert fold;
                double db = 0.0, zeros = 0.0;
                measure(fold, 3, drive, db, zeros);
                logMessage("Fold drive " + juce::String(drive) + ": " + juce::String(db, 2) + " dB, zeros "
                           + juce::String(zeros * 100.0, 2) + " %");
                expect(zeros < 0.05, "Fold drive " + juce::String(drive) + ": " + juce::String(zeros * 100.0, 1)
                                     + " % of the samples are exactly zero");
                lo = std::min(lo, db);
                hi = std::max(hi, db);
            }
            expect(hi - lo <= 12.0, "Fold RMS spread across Drive 25..100 is " + juce::String(hi - lo, 2) + " dB (limit 12)");
        }

        beginTest("SoftClip and HardClip: the level changes by well under 40 dB across Drive 0 / 50 / 100");
        {
            // Makeup is 1/sqrt(preGain), so Drive 100 (pre-gain 100) is about 10 dB down on Drive 0 rather than the
            // 40 dB the old full 1/preGain law gave. 15 dB is the limit; the simulated spread is about 10.
            for (int which = 0; which < 2; ++which)
            {
                double lo = 1.0e9, hi = -1.0e9;
                for (const float drive : { 0.0f, 50.0f, 100.0f })
                {
                    SoftClipInsert soft;
                    HardClipInsert hard;
                    InsertAlgorithmBase& algo = which == 0 ? static_cast<InsertAlgorithmBase&>(soft)
                                                           : static_cast<InsertAlgorithmBase&>(hard);
                    double db = 0.0, zeros = 0.0;
                    measure(algo, which == 0 ? 1 : 2, drive, db, zeros);
                    lo = std::min(lo, db);
                    hi = std::max(hi, db);
                }
                logMessage(juce::String(which == 0 ? "SoftClip" : "HardClip") + " RMS spread across Drive 0/50/100: "
                           + juce::String(hi - lo, 2) + " dB");
                expect(hi - lo < 15.0, juce::String(which == 0 ? "SoftClip" : "HardClip") + " level spread is "
                                       + juce::String(hi - lo, 2) + " dB");
                expect(hi > -40.0, "the output is not silent");
            }
        }
    }
};

static DriveInsertLevelTest driveInsertLevelTest;

// ── Notch12 / Bp12 ──────────────────────────────────────────────────────────────────────────────────
class SvfResponseTest : public juce::UnitTest
{
public:
    SvfResponseTest() : juce::UnitTest("12 dB SVF: Notch12 depth and Bp12 passband at the cutoff", "DSP") {}

    // Steady-state RMS of a 0.5-amplitude sine at `freq` through `f` (cutoff / resonance fixed), after 20 blocks of settling.
    static double steadyRms(FilterAlgorithmBase& f, double sr, float cutoff, float resonance, double freq)
    {
        constexpr int kBlock = 512, kBlocks = 24, kMeasured = 4;
        f.prepare(sr, kBlock, 2);
        juce::AudioBuffer<float> buf(2, kBlock);
        double sumSq = 0.0;
        int count = 0;
        for (int b = 0; b < kBlocks; ++b)
        {
            for (int i = 0; i < kBlock; ++i)
            {
                const float s = 0.5f * (float) std::sin(kTwoPi * freq * (double) (b * kBlock + i) / sr);
                buf.setSample(0, i, s);
                buf.setSample(1, i, s);
            }
            f.process(buf, kBlock, 2, cutoff, resonance);
            if (b >= kBlocks - kMeasured)
                for (int i = 0; i < kBlock; ++i)
                {
                    sumSq += (double) buf.getSample(0, i) * (double) buf.getSample(0, i);
                    ++count;
                }
        }
        return std::sqrt(sumSq / (double) count);
    }

    void runTest() override
    {
        constexpr double kSr = 48000.0;
        constexpr float kCutoff = 1000.0f;
        const double inputRms = 0.5 / std::sqrt(2.0);

        beginTest("Notch12: a tone at the cutoff is attenuated by at least 20 dB at every resonance");
        {
            // JUCE's SVF band-pass has centre gain Q, so dry minus band-pass only nulled at Q = 1. The fix scales the
            // band-pass by 1/Q; the null is then exact in theory, so 20 dB is a wide margin for float rounding.
            for (const float res : { 0.0f, 0.2f, 0.5f, 0.99f })
            {
                Notch12Filter f;
                const double out = steadyRms(f, kSr, kCutoff, res, (double) kCutoff);
                const double att = -toDb(out / inputRms);
                logMessage("Notch12 resonance " + juce::String(res) + ": " + juce::String(att, 1) + " dB down");
                expect(att >= 20.0, "Notch12 at resonance " + juce::String(res) + " is only " + juce::String(att, 1)
                                    + " dB down at the cutoff");
            }
        }

        beginTest("Notch12: a tone three octaves away passes almost untouched");
        {
            Notch12Filter f;
            const double out = steadyRms(f, kSr, kCutoff, 0.5f, 8000.0);
            expect(std::abs(toDb(out / inputRms)) < 1.5, "the notch is not a broad cut (" + juce::String(toDb(out / inputRms), 2) + " dB)");
        }

        beginTest("Bp12: the passband gain at the cutoff is 0 dB (+/-1.5 dB) at every resonance");
        {
            // The raw JUCE band-pass has gain Q at its centre (about +18 dB at resonance 0.99); the fix multiplies by 1/Q.
            for (const float res : { 0.0f, 0.2f, 0.5f, 0.99f })
            {
                Bp12Filter f;
                const double out = steadyRms(f, kSr, kCutoff, res, (double) kCutoff);
                const double gain = toDb(out / inputRms);
                logMessage("Bp12 resonance " + juce::String(res) + ": " + juce::String(gain, 2) + " dB");
                expect(std::abs(gain) <= 1.5, "Bp12 at resonance " + juce::String(res) + " has " + juce::String(gain, 2)
                                              + " dB at the cutoff");
            }
        }
    }
};

static SvfResponseTest svfResponseTest;

// ── Vocoder ─────────────────────────────────────────────────────────────────────────────────────────
class VocoderSpectrumTest : public juce::UnitTest
{
public:
    VocoderSpectrumTest() : juce::UnitTest("Vocoder: the output follows the modulator's spectrum and stays in range", "DSP") {}

    void runTest() override
    {
        constexpr double kSr = 48000.0;

        beginTest("a 1 kHz modulator on a white-noise carrier puts the energy near 1 kHz, not 5 kHz");
        {
            constexpr int kLen = 48000;   // 1 s; the second half is analysed
            VocoderInsert v(false);
            v.prepare(kSr, 512);
            v.setSlotIndex(0);
            VoiceParams p;
            p.insertAlgo = 12;
            setSlotActual(p, 0, 2.0f);   // Wave: White noise
            setSlotActual(p, 1, 0.0f);   // Unison (ignored by noise)
            setSlotActual(p, 2, 1.0f);   // Octave
            setSlotActual(p, 3, 1.0f);   // Note

            const auto y = renderSine(v, p, kSr, 1000.0, 0.3f, kLen, 512);
            const int start = kLen / 2, len = 16384;
            const double near1k = bandPower(y, start, len, 800.0, 1200.0, 9, kSr);
            const double near5k = bandPower(y, start, len, 4600.0, 5400.0, 9, kSr);
            const double diffDb = 10.0 * std::log10(juce::jmax(1.0e-30, near1k) / juce::jmax(1.0e-30, near5k));
            logMessage("vocoder band energy, 1 kHz vs 5 kHz: " + juce::String(diffDb, 1) + " dB");
            expect(near1k > 1.0e-6, "the vocoder rendered silence near 1 kHz");
            expect(diffDb >= 10.0, "energy near 1 kHz is only " + juce::String(diffDb, 1) + " dB above 5 kHz (limit 10)");
        }

        beginTest("a full-scale input stays inside 1.0 once the envelopes have settled (saw carrier, 13 unison voices)");
        {
            // The vocoder's makeup gain (kOutputGain) was fitted by simulation on broadband input. A float64 port of
            // the product (the same bands, filters, followers and carrier) gave, for a full-scale sine, a settled peak
            // of 0.88 at 100 Hz and under 0.5 from 200 Hz up, but a 16 ms onset transient of up to 1.9 for a low
            // frequency (1.95 at 200 Hz, 1.09 at 1 kHz): that overshoot is logged, not asserted, and is a gain-staging
            // question for the owner (see the backlog row). The settled peak is asserted below 1.0.
            constexpr int kSettled = 6000;
            for (const double freq : { 200.0, 1000.0, 4000.0 })
            {
                VocoderInsert v(false);
                v.prepare(kSr, 512);
                v.setSlotIndex(0);
                VoiceParams p;
                p.insertAlgo = 12;
                setSlotActual(p, 0, 0.0f);   // Wave: Saw
                setSlotActual(p, 1, 6.0f);   // Unison: 13 voices
                setSlotActual(p, 2, 2.0f);   // Octave
                setSlotActual(p, 3, 1.0f);   // Note: C
                const auto y = renderSine(v, p, kSr, freq, 1.0f, 12000, 512);
                float peak = 0.0f, settledPeak = 0.0f;
                for (size_t i = 0; i < y.size(); ++i)
                {
                    peak = juce::jmax(peak, std::abs(y[i]));
                    if ((int) i >= kSettled) settledPeak = juce::jmax(settledPeak, std::abs(y[i]));
                }
                logMessage("vocoder peak with a full-scale " + juce::String(freq, 0) + " Hz input: whole render "
                           + juce::String(peak, 3) + ", settled " + juce::String(settledPeak, 3));
                // The output soft limiter (VocoderInsert::softLimit) keeps every sample inside +/-1, onset included.
                expect(std::isfinite(peak) && peak <= 1.0f,
                       "peak " + juce::String(peak, 4) + " at " + juce::String(freq, 0) + " Hz");
                expect(settledPeak < 1.0f,
                       "settled peak " + juce::String(settledPeak, 3) + " at " + juce::String(freq, 0) + " Hz");
            }
        }
    }
};

static VocoderSpectrumTest vocoderSpectrumTest;

// ── X-Mod SSB (Hilbert transformer) ─────────────────────────────────────────────────────────────────
class HilbertImageTest : public juce::UnitTest
{
public:
    HilbertImageTest() : juce::UnitTest("X-Mod SSB: Hilbert transformer image rejection and quadrature balance", "DSP") {}

    void runTest() override
    {
        constexpr double kSr = 44100.0;
        constexpr double kShiftHz = 500.0;
        constexpr int kTotal = 8192 + 16384;   // settling, then the analysed span

        beginTest("an upper-sideband shift of 500 Hz beats its image by at least 35 dB, and re / im have equal level");
        {
            // Simulated image rejection at 44.1 kHz: 42.9 / 60.3 / 45.8 dB at 300 Hz / 1 kHz / 4 kHz. The old
            // transformer measured 27.2 / 18.4 / 5.7 dB, so 35 dB separates the two with 8 dB of headroom.
            for (const double f0 : { 300.0, 1000.0, 4000.0 })
            {
                mu_wavetable::HilbertTransform h;
                std::vector<float> ssb((size_t) kTotal), re((size_t) kTotal), im((size_t) kTotal);
                for (int n = 0; n < kTotal; ++n)
                {
                    const float x = (float) std::sin(kTwoPi * f0 * (double) n / kSr);
                    const auto q = h.process(x);
                    const double th = kTwoPi * kShiftHz * (double) n / kSr;
                    // The shifter's upper sideband, as XModOscPair forms it.
                    ssb[(size_t) n] = (float) ((double) q.re * std::cos(th) - (double) q.im * std::sin(th));
                    re[(size_t) n] = q.re;
                    im[(size_t) n] = q.im;
                }
                const int start = 8192, len = 16384;
                const double wanted = tonePower(ssb, start, len, f0 + kShiftHz, kSr);
                const double image  = tonePower(ssb, start, len, std::abs(f0 - kShiftHz), kSr);
                const double rejectionDb = 10.0 * std::log10(juce::jmax(1.0e-30, wanted) / juce::jmax(1.0e-30, image));
                const double balanceDb = toDb(rmsOf(im, start, start + len) / juce::jmax(1.0e-12, rmsOf(re, start, start + len)));
                logMessage("Hilbert " + juce::String(f0, 0) + " Hz: image rejection " + juce::String(rejectionDb, 1)
                           + " dB, im/re level " + juce::String(balanceDb, 3) + " dB");
                expect(rejectionDb >= 35.0, juce::String(f0, 0) + " Hz: the image is only " + juce::String(rejectionDb, 1)
                                            + " dB below the wanted sideband");
                expect(std::abs(balanceDb) <= 0.5, juce::String(f0, 0) + " Hz: re and im differ by " + juce::String(balanceDb, 3) + " dB");
            }
        }
    }
};

static HilbertImageTest hilbertImageTest;

// ── Karplus-Strong pitch ────────────────────────────────────────────────────────────────────────────
class KarplusPitchTest : public juce::UnitTest
{
public:
    KarplusPitchTest() : juce::UnitTest("Karplus-Strong: pitch stays on the note when the loop low-pass is lowered", "DSP") {}

    // Pitch error in cents of the periodic signal x[start, start + len), found by the peak of the normalised
    // autocorrelation within +/-30 % of the expected period (refined by a parabola). The normalisation divides out
    // the decay of the string, which would otherwise pull the peak to a shorter lag.
    static double pitchErrorCents(const std::vector<float>& x, int start, int len, double sr, double targetHz)
    {
        const double period = sr / targetHz;
        const int lagLo = (int) std::floor(0.7 * period), lagHi = (int) std::ceil(1.3 * period);
        const int span = len + lagHi + 2;

        // The span's mean is removed first: the decaying DC of the pulse train would bias the correlation.
        double mean = 0.0;
        for (int i = 0; i < span; ++i) mean += (double) x[(size_t) (start + i)];
        mean /= (double) span;

        std::vector<double> r((size_t) lagHi + 2, 0.0);
        for (int lag = lagLo - 1; lag <= lagHi + 1; ++lag)
        {
            double sum = 0.0, e1 = 0.0, e2 = 0.0;
            for (int i = 0; i < len; ++i)
            {
                const double a = (double) x[(size_t) (start + i)] - mean;
                const double b = (double) x[(size_t) (start + i + lag)] - mean;
                sum += a * b; e1 += a * a; e2 += b * b;
            }
            r[(size_t) lag] = sum / std::sqrt(juce::jmax(1.0e-30, e1 * e2));
        }
        int best = lagLo;
        for (int lag = lagLo; lag <= lagHi; ++lag)
            if (r[(size_t) lag] > r[(size_t) best]) best = lag;

        const double y0 = r[(size_t) best - 1], y1 = r[(size_t) best], y2 = r[(size_t) best + 1];
        const double denom = y0 - 2.0 * y1 + y2;
        const double delta = std::abs(denom) > 1.0e-12 ? 0.5 * (y0 - y2) / denom : 0.0;
        return 1200.0 * std::log2((sr / ((double) best + delta)) / targetHz);
    }

    void runTest() override
    {
        constexpr double kSr = 48000.0;

        beginTest("an impulse-excited string plays within 8 cents of its note at LPF 1 kHz and 5 kHz");
        {
            // The delay line is shortened by the loop low-pass's phase delay. The old code ignored it and played
            // 31 cents flat (C, LPF 1 kHz) and 18 cents flat (C, LPF 5 kHz). The simulated error of the new code is
            // +2 / +0 / +0 cents with this window, so 8 cents is the limit. The very dark LPF settings are left out:
            // autocorrelation picks octave errors there, which says nothing about the string.
            struct Case { int note, octave; float lpfHz; const char* name; };
            const Case cases[] = { { 0, 2, 1000.0f, "C 130.8 Hz, LPF 1 kHz" },
                                   { 0, 2, 5000.0f, "C 130.8 Hz, LPF 5 kHz" },
                                   { 11, 3, 5000.0f, "B 493.9 Hz, LPF 5 kHz" } };
            for (const auto& c : cases)
            {
                KarplusStrongInsert k;
                k.prepare(kSr, 4096);
                VoiceParams p;
                p.insertAlgo = 11;
                setSlotActual(p, 0, (float) c.note);
                setSlotActual(p, 1, (float) c.octave);
                setSlotActual(p, 2, 63.16f);   // Feedback knob 63.16 % -> loop gain 1 - (1 - 0.6316)^3 = 0.95
                setSlotActual(p, 3, c.lpfHz);

                float gr = 0.0f;
                // A silent block first, so the 15 ms pitch and 10 ms gain ramps have reached their targets.
                juce::AudioBuffer<float> settle(1, 4096);
                settle.clear();
                k.process(settle, 4096, 1, p, gr);

                // Then one impulse and the ringing that follows.
                constexpr int kRing = 8192;
                juce::AudioBuffer<float> ring(1, kRing);
                ring.clear();
                ring.setSample(0, 0, 1.0f);
                k.process(ring, kRing, 1, p, gr);
                std::vector<float> x((size_t) kRing);
                for (int i = 0; i < kRing; ++i) x[(size_t) i] = ring.getSample(0, i);

                const double targetHz = 32.7 * std::pow(2.0, (double) (c.note + 12 * c.octave) / 12.0);
                const double cents = pitchErrorCents(x, 2048, 4096, kSr, targetHz);
                logMessage(juce::String(c.name) + ": " + juce::String(cents, 2) + " cents");
                expect(std::abs(cents) <= 8.0, juce::String(c.name) + " is " + juce::String(cents, 2) + " cents off");
            }
        }
    }
};

static KarplusPitchTest karplusPitchTest;
