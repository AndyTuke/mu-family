#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "Audio/AlgorithmNames.h"
#include "Audio/FX/Algorithms/CombInsert.h"
#include "Audio/FX/Algorithms/FreqShiftInsert.h"
#include "Audio/FX/Algorithms/FormantInsert.h"
#include "Audio/FX/Algorithms/StutterInsert.h"
#include "Audio/FX/Algorithms/LoFiInsert.h"
#include "Audio/FX/Algorithms/HilbertIir.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <random>
#include <vector>

// The Comb (25), Freq Shift (26), Formant (27), Stutter (28) and Lo-Fi (29) inserts: each is untouched where its mix or amounts are
// neutral, does what its knobs say (the comb rings at the pitch, the shifter moves every partial by the same Hz and rejects the other
// sideband, the formant peaks sit on the vowel table, the stutter repeats the slice sample-exactly, the lo-fi wobbles and crackles at
// the stated rates), stays finite at every rate and block size, and resets to a fresh state. All offline; nothing is listened to.
struct FiveInsertsTests : juce::UnitTest
{
    FiveInsertsTests() : juce::UnitTest("New inserts: comb, freq shift, formant, stutter, lo-fi", "mu-core") {}

    static constexpr double kSR = 48000.0;
    enum { Comb = 25, FreqShift = 26, Formant = 27, Stutter = 28, LoFi = 29 };

    // Knob values in their real units, stored the way a preset stores them (normalised).
    static VoiceParams params(int algo, float a0, float a1, float a2, float a3)
    {
        VoiceParams p;
        p.insertAlgo = algo;
        const float v[4] = { a0, a1, a2, a3 };
        for (int s = 0; s < 4; ++s) p.insertParam[s] = mu_ui::actualToNorm(v[s], algo, s);
        return p;
    }

    using Wave = std::vector<float>;

    static Wave sine(int n, double f, double sr, float amp)
    {
        Wave v((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = amp * (float) std::sin(2.0 * juce::MathConstants<double>::pi * f * (double) i / sr);
        return v;
    }
    static Wave noise(int n, unsigned seed, float amp = 0.5f)
    {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> d(-amp, amp);
        Wave v((size_t) n);
        for (auto& x : v) x = d(rng);
        return v;
    }
    // Pink noise (Paul Kellet's economy filter on white noise), roughly 0.2 RMS.
    static Wave pink(int n, unsigned seed)
    {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> d(-1.0f, 1.0f);
        float b0 = 0, b1 = 0, b2 = 0;
        Wave v((size_t) n);
        for (auto& x : v)
        {
            const float w = d(rng);
            b0 = 0.99765f * b0 + w * 0.0990460f;
            b1 = 0.96300f * b1 + w * 0.2965164f;
            b2 = 0.57000f * b2 + w * 1.0526913f;
            x = 0.1f * (b0 + b1 + b2 + w * 0.1848f);
        }
        return v;
    }

    // Runs a stereo input (the same signal on both channels) through an insert in blocks; the left channel is returned.
    static Wave render(InsertAlgorithmBase& fx, const VoiceParams& p, const Wave& in, int block = 512)
    {
        Wave out(in.size());
        juce::AudioBuffer<float> buf(2, block);
        for (size_t pos = 0; pos < in.size(); pos += (size_t) block)
        {
            const int n = (int) std::min<size_t>((size_t) block, in.size() - pos);
            for (int i = 0; i < n; ++i)
            {
                buf.setSample(0, i, in[pos + (size_t) i]);
                buf.setSample(1, i, in[pos + (size_t) i]);
            }
            float gr = 0.0f;
            fx.process(buf, n, 2, p, gr);
            for (int i = 0; i < n; ++i) out[pos + (size_t) i] = buf.getSample(0, i);
        }
        return out;
    }

    static double mag(const Wave& y, double f, double sr, int start, int n)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f / sr;
        double re = 0.0, im = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double v = (double) y[(size_t) (start + i)];
            re += v * std::cos(w * (double) i);
            im -= v * std::sin(w * (double) i);
        }
        return 2.0 * std::sqrt(re * re + im * im) / (double) n;
    }
    static double db(double ratio) { return 20.0 * std::log10(std::max(ratio, 1e-12)); }
    static double rms(const Wave& y, int start, int n)
    {
        double s = 0.0;
        for (int i = 0; i < n; ++i) s += (double) y[(size_t) (start + i)] * (double) y[(size_t) (start + i)];
        return std::sqrt(s / (double) n);
    }
    static bool allFinite(const Wave& y) { for (float v : y) if (! std::isfinite(v)) return false; return true; }
    static float peakOf(const Wave& y) { float m = 0.0f; for (float v : y) m = std::max(m, std::abs(v)); return m; }

    // Averaged power spectrum (dB per bin) of a long signal: Hann frames of 16384 with 50 % overlap; bin k is k * sr / 16384.
    static std::vector<double> spectrum(const Wave& y)
    {
        constexpr int order = 14, N = 1 << order;
        juce::dsp::FFT fft(order);
        std::vector<float> data((size_t) (2 * N));
        std::vector<double> acc((size_t) (N / 2 + 1), 0.0);
        int frames = 0;
        for (size_t start = 0; start + N <= y.size(); start += N / 2)
        {
            for (int i = 0; i < N; ++i)
            {
                const float w = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * (float) i / (float) N);
                data[(size_t) i] = y[start + (size_t) i] * w;
            }
            std::fill(data.begin() + N, data.end(), 0.0f);
            fft.performRealOnlyForwardTransform(data.data(), true);
            for (int k = 0; k <= N / 2; ++k)
            {
                const double re = data[(size_t) (2 * k)], im = data[(size_t) (2 * k + 1)];
                acc[(size_t) k] += re * re + im * im;
            }
            ++frames;
        }
        for (auto& a : acc) a = 10.0 * std::log10(std::max(a / std::max(1, frames), 1e-30));
        return acc;
    }
    // The strongest bin in [lo, hi] Hz and its level, plus the level at each window edge.
    struct Peak { double hz = 0.0, level = -300.0, edge = -300.0; };
    static Peak peakIn(const std::vector<double>& spec, double sr, double lo, double hi)
    {
        const double binHz = sr / 16384.0;
        const int a = (int) std::floor(lo / binHz), b = (int) std::ceil(hi / binHz);
        Peak p;
        for (int k = a; k <= b && k < (int) spec.size(); ++k)
            if (spec[(size_t) k] > p.level) { p.level = spec[(size_t) k]; p.hz = (double) k * binHz; }
        p.edge = std::max(spec[(size_t) a], spec[(size_t) std::min(b, (int) spec.size() - 1)]);
        return p;
    }

    // The pitch a Comb rings at, from the autocorrelation peak of its output for noise input (parabolic interpolation).
    static double combHz(double sr, float freq, float feedbackPct)
    {
        mu_audio::CombInsert fx;
        fx.prepare(sr, 512);
        const int n = (int) sr;
        const auto y = render(fx, params(Comb, freq, feedbackPct, 0.0f, 100.0f), noise(n, 4));
        const int from = n / 4, len = n / 2;
        const double d = sr / (double) freq;
        const int lo = (int) std::floor(0.8 * d), hi = (int) std::ceil(1.2 * d);
        std::vector<double> r((size_t) (hi - lo + 3), 0.0);
        for (int lag = lo - 1; lag <= hi + 1; ++lag)
        {
            double s = 0.0;
            for (int i = 0; i < len; ++i) s += (double) y[(size_t) (from + i)] * (double) y[(size_t) (from + i - lag)];
            r[(size_t) (lag - lo + 1)] = s;
        }
        int best = 1;
        for (int k = 1; k < (int) r.size() - 1; ++k) if (r[(size_t) k] > r[(size_t) best]) best = k;
        const double a = r[(size_t) best - 1], b = r[(size_t) best], c = r[(size_t) best + 1];
        const double off = 0.5 * (a - c) / (a - 2.0 * b + c);
        return sr / ((double) (best - 1 + lo) + off);
    }

    // The impulse response (left channel) of an insert for the given knobs.
    static Wave impulse(InsertAlgorithmBase& fx, const VoiceParams& p, int n)
    {
        Wave in((size_t) n, 0.0f);
        in[0] = 1.0f;
        return render(fx, p, in);
    }

    void runTest() override
    {
        // ── Comb ─────────────────────────────────────────────────────────────────────────────────────────────────────
        beginTest("Comb: the resonant pitch is within 10 cents of Freq at 220 and 1000 Hz, at 44.1 and 96 kHz");
        {
            for (double sr : { 44100.0, 96000.0 })
                for (float f : { 220.0f, 1000.0f })
                {
                    const double hz = combHz(sr, f, 80.0f);
                    const double cents = 1200.0 * std::log2(hz / (double) f);
                    logMessage("Comb " + juce::String(f, 0) + " Hz at " + juce::String(sr, 0) + ": " + juce::String(cents, 2) + " cents");
                    expect(std::abs(cents) < 10.0, "Comb " + juce::String(f, 0) + " Hz at " + juce::String(sr, 0) + " is " + juce::String(cents, 2) + " cents off");
                }
        }

        beginTest("Comb: negative Feedback rings at odd multiples of half the pitch only, and Damping shortens the ring");
        {
            const int n = 48000;
            mu_audio::CombInsert neg;
            neg.prepare(kSR, 512);
            const auto h = impulse(neg, params(Comb, 1000.0f, -80.0f, 0.0f, 100.0f), n);
            // Negative feedback at 1000 Hz resonates at 500, 1500, 2500 Hz and notches 1000, 2000, 3000 Hz.
            const double odd  = (mag(h, 500.0, kSR, 0, n) + mag(h, 1500.0, kSR, 0, n) + mag(h, 2500.0, kSR, 0, n)) / 3.0;
            const double even = (mag(h, 1000.0, kSR, 0, n) + mag(h, 2000.0, kSR, 0, n) + mag(h, 3000.0, kSR, 0, n)) / 3.0;
            expect(db(odd / even) > 12.0, "odd resonances only " + juce::String(db(odd / even), 1) + " dB above the notches");

            // Ring time: the first 10 ms window whose RMS falls below 1 % of the first one.
            auto ringMs = [&](float damping)
            {
                mu_audio::CombInsert fx;
                fx.prepare(kSR, 512);
                auto r = impulse(fx, params(Comb, 220.0f, 80.0f, damping, 100.0f), n);
                // Damping darkens the repeats, so the ring is measured on the highs (a second difference, a crude high-pass).
                for (int i = n - 1; i >= 2; --i) r[(size_t) i] = r[(size_t) i] - 2.0f * r[(size_t) i - 1] + r[(size_t) i - 2];
                const int w = 480;
                const double first = rms(r, 0, w);
                for (int s = w; s + w < n; s += w)
                    if (rms(r, s, w) < 0.01 * first) return s * 1000.0 / kSR;
                return 1000.0;
            };
            const double open = ringMs(0.0f), dark = ringMs(100.0f);
            logMessage("Comb ring: Damping 0 " + juce::String(open, 0) + " ms, Damping 100 " + juce::String(dark, 0) + " ms");
            expect(dark < 0.5 * open, "Damping 100 rings " + juce::String(dark, 0) + " ms against " + juce::String(open, 0) + " ms");
        }

        beginTest("Comb: Mix 0 leaves the signal untouched, the level stays within 3 dB across Feedback, and Feedback 95 at 20 Hz decays");
        {
            const auto x = noise(30000, 8);
            mu_audio::CombInsert fx;
            fx.prepare(kSR, 512);
            expect(render(fx, params(Comb, 220.0f, 70.0f, 30.0f, 0.0f), x) == x, "Mix 0 is bit-exact dry");
            for (float fb : { 0.0f, 50.0f, 95.0f, -95.0f })
            {
                mu_audio::CombInsert c;
                c.prepare(kSR, 512);
                const auto big = noise(96000, 6);
                const auto y = render(c, params(Comb, 220.0f, fb, 0.0f, 100.0f), big);
                const double d = db(rms(y, 48000, 40000) / rms(big, 48000, 40000));
                expect(std::abs(d) < 3.0, "Feedback " + juce::String(fb, 0) + " changes the level by " + juce::String(d, 2) + " dB");
            }
            mu_audio::CombInsert slow;
            slow.prepare(kSR, 512);
            Wave burst = noise(4800, 2, 0.8f);
            burst.resize(480000, 0.0f);   // a burst, then ten seconds of silence
            const auto y = render(slow, params(Comb, 20.0f, 95.0f, 0.0f, 100.0f), burst);
            expect(allFinite(y), "finite");
            expect(rms(y, 48000, 48000) > rms(y, 432000, 48000), "the ring dies away (no growth over 10 s of silence)");
        }

        // ── Freq Shift ───────────────────────────────────────────────────────────────────────────────────────────────
        beginTest("Freq Shift: a 1 kHz sine shifted by +200 Hz is strongest at 1200 Hz, with the original and the other sideband 35 dB down; a negative shift mirrors it");
        {
            for (double sr : { 44100.0, 96000.0 })
                for (float shift : { 200.0f, -200.0f })
                {
                    mu_audio::FreqShiftInsert fx;
                    fx.prepare(sr, 512);
                    const int n = (int) sr;
                    const auto y = render(fx, params(FreqShift, shift, 0.0f, 0.0f, 100.0f), sine(n, 1000.0, sr, 0.5f));
                    const int start = n / 2, len = n / 2;
                    const double wantedHz = 1000.0 + (double) shift, otherHz = 1000.0 - (double) shift;
                    const double main = mag(y, wantedHz, sr, start, len);
                    const double other = mag(y, otherHz, sr, start, len);
                    const double orig = mag(y, 1000.0, sr, start, len);
                    logMessage("FreqShift " + juce::String(shift, 0) + " at " + juce::String(sr, 0) + ": main " + juce::String(main, 3)
                               + ", opposite " + juce::String(db(other / main), 1) + " dB, original " + juce::String(db(orig / main), 1) + " dB");
                    expect(main > 0.4 && main < 0.6, "the shifted tone keeps its level (" + juce::String(main, 3) + ")");
                    expect(db(other / main) < -35.0, "opposite sideband " + juce::String(db(other / main), 1) + " dB at shift " + juce::String(shift, 0));
                    expect(db(orig / main) < -35.0, "the original " + juce::String(db(orig / main), 1) + " dB at shift " + juce::String(shift, 0));
                }
        }

        beginTest("Freq Shift: the Hilbert pair stays within 1 degree of 90 at 1 kHz and 5 kHz, Mix 0 is untouched, Feedback 80 stays bounded, the level is kept, reset equals fresh");
        {
            for (double f : { 1000.0, 5000.0 })
            {
                mu_audio::HilbertIir h;
                const int n = 40000;
                double ire = 0, iim = 0, qre = 0, qim = 0;
                for (int i = 0; i < n; ++i)
                {
                    const float x = (float) std::sin(2.0 * juce::MathConstants<double>::pi * f * (double) i / 44100.0);
                    float I, Q;
                    h.process(x, I, Q);
                    if (i < 4000) continue;   // settle
                    const double c = std::cos(2.0 * juce::MathConstants<double>::pi * f * (double) i / 44100.0);
                    const double s = std::sin(2.0 * juce::MathConstants<double>::pi * f * (double) i / 44100.0);
                    ire += I * c; iim += I * s; qre += Q * c; qim += Q * s;
                }
                const double diff = std::atan2(iim, ire) - std::atan2(qim, qre);   // the I branch leads the Q branch
                double deg = diff * 180.0 / juce::MathConstants<double>::pi;
                while (deg > 180.0) deg -= 360.0;
                while (deg < -180.0) deg += 360.0;
                logMessage("Hilbert pair at " + juce::String(f, 0) + " Hz: " + juce::String(deg, 2) + " degrees");
                expect(std::abs(std::abs(deg) - 90.0) < 1.0, "phase difference " + juce::String(deg, 2) + " degrees at " + juce::String(f, 0) + " Hz");
            }

            const auto x = noise(30000, 12);
            mu_audio::FreqShiftInsert dry;
            dry.prepare(kSR, 512);
            expect(render(dry, params(FreqShift, 60.0f, 0.0f, 0.0f, 0.0f), x) == x, "Mix 0 is bit-exact dry");

            mu_audio::FreqShiftInsert fbk;
            fbk.prepare(kSR, 512);
            const auto big = render(fbk, params(FreqShift, 300.0f, 0.0f, 80.0f, 100.0f), noise(240000, 3, 0.5f));
            expect(allFinite(big) && peakOf(big) < 40.0f, "Feedback 80 stays bounded, peak " + juce::String(peakOf(big), 2));

            mu_audio::FreqShiftInsert lvl;
            lvl.prepare(kSR, 512);
            const auto wide = noise(96000, 5);
            const auto y = render(lvl, params(FreqShift, 60.0f, 0.0f, 0.0f, 100.0f), wide);
            const double d = db(rms(y, 24000, 60000) / rms(wide, 24000, 60000));
            expect(std::abs(d) < 1.0, "level change " + juce::String(d, 2) + " dB");

            mu_audio::FreqShiftInsert used, fresh;
            used.prepare(kSR, 512); fresh.prepare(kSR, 512);
            const auto p = params(FreqShift, 123.0f, 3.0f, 30.0f, 70.0f);
            render(used, p, noise(5000, 1));
            used.reset();
            expect(render(used, p, x) == render(fresh, p, x), "reset equals fresh");
        }

        // ── Formant ──────────────────────────────────────────────────────────────────────────────────────────────────
        beginTest("Formant: white noise shows three peaks within 15 % of the vowel table at A, E, I, O and U, 12.5 % lies between A and E, Shift +12 doubles them");
        {
            static const float table[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 350, 1700, 2700 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
            const auto white = noise(262144, 21, 0.5f);
            auto spec = [&](float vowel, float shiftSt, float res)
            {
                mu_audio::FormantInsert fx;
                fx.prepare(kSR, 512);
                return spectrum(render(fx, params(Formant, vowel, shiftSt, res, 100.0f), white));
            };
            for (int v = 0; v < 5; ++v)
            {
                const auto s = spec((float) v * 25.0f, 0.0f, 60.0f);
                for (int k = 0; k < 3; ++k)
                {
                    const double F = (double) table[v][k];
                    const auto pk = peakIn(s, kSR, 0.75 * F, 1.3 * F);
                    logMessage("Formant vowel " + juce::String(v) + " F" + juce::String(k + 1) + " table " + juce::String(F, 0) + " found " + juce::String(pk.hz, 0));
                    expect(std::abs(pk.hz - F) / F <= 0.15 && pk.level > pk.edge + 3.0,
                           "vowel " + juce::String(v) + " F" + juce::String(k + 1) + " expected " + juce::String(F, 0) + " found " + juce::String(pk.hz, 0));
                }
            }
            const auto mid = spec(12.5f, 0.0f, 60.0f);
            const auto m1 = peakIn(mid, kSR, 450.0, 760.0);
            expect(m1.hz > 480.0 && m1.hz < 700.0, "12.5 % puts F1 between A (800) and E (400): " + juce::String(m1.hz, 0));
            const auto up = spec(0.0f, 12.0f, 60.0f);
            const auto u1 = peakIn(up, kSR, 1200.0, 2000.0);
            expect(std::abs(u1.hz - 1600.0) / 1600.0 <= 0.15, "Shift +12 doubles F1 to about 1600: " + juce::String(u1.hz, 0));

            // -3 dB width of the A F1 peak at the two Resonance corners.
            auto width = [&](float res)
            {
                const auto s = spec(0.0f, 0.0f, res);
                const double binHz = kSR / 16384.0;
                const auto pk = peakIn(s, kSR, 700.0, 900.0);
                int k0 = (int) std::round(pk.hz / binHz), lo = k0, hi = k0;
                while (lo > 1 && s[(size_t) lo] > pk.level - 3.0) --lo;
                while (hi < (int) s.size() - 2 && s[(size_t) hi] > pk.level - 3.0) ++hi;
                return (double) (hi - lo) * binHz;
            };
            const double wLow = width(0.0f), wHigh = width(100.0f);
            logMessage("Formant -3 dB width at F1: Resonance 0 " + juce::String(wLow, 0) + " Hz, 100 " + juce::String(wHigh, 0) + " Hz");
            expect(wHigh < wLow, "Resonance 100 is narrower than 0");
        }

        beginTest("Formant: the pink-noise level stays within 6 dB across vowels and Resonance corners, Mix 0 is untouched, a Vowel sweep does not click");
        {
            const auto pk = pink(96000, 4);
            double worst = 0.0;
            for (float v : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f })
                for (float r : { 0.0f, 100.0f })
                {
                    mu_audio::FormantInsert fx;
                    fx.prepare(kSR, 512);
                    const auto y = render(fx, params(Formant, v, 0.0f, r, 100.0f), pk);
                    const double d = db(rms(y, 24000, 60000) / rms(pk, 24000, 60000));
                    worst = std::max(worst, std::abs(d));
                    logMessage("Formant level vowel " + juce::String(v, 0) + " resonance " + juce::String(r, 0) + ": " + juce::String(d, 2) + " dB");
                }
            expect(worst <= 6.0, "worst level change " + juce::String(worst, 2) + " dB");

            const auto x = noise(30000, 14);
            mu_audio::FormantInsert fx;
            fx.prepare(kSR, 512);
            expect(render(fx, params(Formant, 50.0f, 3.0f, 40.0f, 0.0f), x) == x, "Mix 0 is bit-exact dry");

            // A sweep of Vowel across its range in 100 ms: render in 64-sample blocks with the knob moving each block.
            mu_audio::FormantInsert sweep;
            sweep.prepare(kSR, 64);
            const Wave in = sine(9600, 220.0, kSR, 0.3f);
            Wave out(in.size());
            juce::AudioBuffer<float> buf(2, 64);
            for (int b = 0; b < (int) in.size() / 64; ++b)
            {
                const auto p = params(Formant, 100.0f * (float) b / (float) (in.size() / 64 - 1), 0.0f, 40.0f, 100.0f);
                for (int i = 0; i < 64; ++i) { buf.setSample(0, i, in[(size_t) (b * 64 + i)]); buf.setSample(1, i, in[(size_t) (b * 64 + i)]); }
                float gr = 0.0f;
                sweep.process(buf, 64, 2, p, gr);
                for (int i = 0; i < 64; ++i) out[(size_t) (b * 64 + i)] = buf.getSample(0, i);
            }
            float step = 0.0f;
            for (size_t i = 1; i < out.size(); ++i) step = std::max(step, std::abs(out[i] - out[i - 1]));
            expect(allFinite(out) && step < 0.2f, "largest sample-to-sample step during the sweep " + juce::String(step, 3));
        }

        // ── Stutter ──────────────────────────────────────────────────────────────────────────────────────────────────
        beginTest("Stutter: Repeat 0 leaves the signal untouched");
        {
            const auto x = noise(60000, 31);
            mu_audio::StutterInsert fx;
            fx.prepare(kSR, 512);
            expect(render(fx, params(Stutter, 50.0f, 0.0f, 0.0f, 100.0f), x) == x, "bit-exact dry");
            expectEquals(fx.hitCount(), 0);
        }

        beginTest("Stutter: Repeat 100 repeats the last slice four times sample-exactly, then a fresh slice");
        {
            const int L = 4800;   // 100 ms at 48 kHz
            const auto x = noise(8 * L, 32);
            mu_audio::StutterInsert fx;
            fx.prepare(kSR, 512);
            const auto y = render(fx, params(Stutter, 100.0f, 100.0f, 0.0f, 100.0f), x);
            bool live = true;
            for (int i = 0; i < L; ++i) live = live && y[(size_t) i] == x[(size_t) i];
            expect(live, "the sound before the first boundary is live");
            int bad = 0;
            for (int pass = 0; pass < 4; ++pass)
                for (int j = 150; j < L - 150; ++j)
                    if (y[(size_t) (L + pass * L + j)] != x[(size_t) (1 + j)]) ++bad;
            expectEquals(bad, 0, "the first four passes replay the slice sample-exactly");
            int fresh = 0;
            for (int j = 150; j < L - 150; ++j)
                if (y[(size_t) (5 * L + j)] != x[(size_t) (4 * L + 1 + j)]) ++fresh;
            expectEquals(fresh, 0, "the fifth pass is a fresh slice of the recent sound");
        }

        beginTest("Stutter: Rev plays the slice backwards, Alt alternates, and Mix 50 is the average of the dry and Mix 100");
        {
            const int L = 4800;
            const auto x = noise(4 * L, 33);
            auto run = [&](float dir, float mixPct)
            {
                mu_audio::StutterInsert fx;
                fx.prepare(kSR, 512);
                return render(fx, params(Stutter, 100.0f, 100.0f, dir, mixPct), x);
            };
            const auto rev = run(1.0f, 100.0f);
            int badRev = 0;
            for (int j = 150; j < L - 150; ++j) if (rev[(size_t) (L + j)] != x[(size_t) (L - j)]) ++badRev;
            expectEquals(badRev, 0, "Rev reads the slice from its newest sample back");
            const auto alt = run(2.0f, 100.0f);
            int badAlt = 0;
            for (int j = 150; j < L - 150; ++j)
            {
                if (alt[(size_t) (L + j)] != x[(size_t) (1 + j)]) ++badAlt;        // pass 0 forwards
                if (alt[(size_t) (2 * L + j)] != x[(size_t) (L - j)]) ++badAlt;    // pass 1 backwards
            }
            expectEquals(badAlt, 0, "Alt goes forwards then backwards");
            const auto half = run(0.0f, 50.0f), full = run(0.0f, 100.0f);
            float worst = 0.0f;
            for (size_t i = 0; i < x.size(); ++i) worst = std::max(worst, std::abs(half[i] - 0.5f * (x[i] + full[i])));
            expect(worst < 1.0e-5f, "Mix 50 equals the average, worst error " + juce::String(worst));
        }

        beginTest("Stutter: Sync follows the tempo (default 120 bpm), a tempo change waits for the boundary, slow tempi cap at 1 s");
        {
            // Four sixteenths at 120 bpm is 0.5 s = 24000 samples: the first boundary, and the replay of input sample 1.
            const auto x = noise(80000, 34);
            VoiceParams p = params(Stutter, 100.0f, 100.0f, 0.0f, 100.0f);
            p.insertParam[0] = mu_ui::insertSyncStepToNorm(4);
            p.insertSync = true;
            mu_audio::StutterInsert fx;
            fx.prepare(kSR, 512);
            const auto y = render(fx, p, x);   // tempoBpm was never set: 120 bpm
            int bad = 0;
            for (int j = 200; j < 24000 - 200; ++j) if (y[(size_t) (24000 + j)] != x[(size_t) (1 + j)]) ++bad;
            expectEquals(bad, 0, "the synced slice is 24000 samples and starts at the first boundary");

            // A tempo change in the middle of the first slice: the boundary stays where it was, the new length applies from it.
            VoiceParams q = params(Stutter, 100.0f, 100.0f, 0.0f, 100.0f);
            q.insertParam[0] = mu_ui::insertSyncStepToNorm(2);   // 0.25 s at 120 bpm: boundary at 12000
            q.insertSync = true;
            mu_audio::StutterInsert tc;
            tc.prepare(kSR, 512);
            Wave out;
            {
                const Wave a(6000, 0.3f), b(34000, 0.3f);
                const auto first = render(tc, q, a);
                tc.tempoBpm = 60.0;
                const auto second = render(tc, q, b);
                out = first;
                out.insert(out.end(), second.begin(), second.end());
            }
            expect(out[11000] == 0.3f, "live before the boundary");
            expect(std::abs(out[12500]) < 1.0e-6f, "the boundary came at 12000 and the slower tempo set a slice of 24000 (mostly empty history)");

            // 16 sixteenths at 60 bpm is 4 s: it caps at 1 s, so one boundary has passed by 1.1 s but none by 0.9 s.
            VoiceParams c = params(Stutter, 100.0f, 100.0f, 0.0f, 100.0f);
            c.insertParam[0] = mu_ui::insertSyncStepToNorm(16);
            c.insertSync = true;
            mu_audio::StutterInsert cap;
            cap.prepare(kSR, 512);
            cap.tempoBpm = 60.0;
            render(cap, c, Wave((int) (0.9 * kSR), 0.2f));
            expectEquals(cap.rollCount(), 0);
            render(cap, c, Wave((int) (0.2 * kSR), 0.2f));
            expectEquals(cap.rollCount(), 1);
        }

        beginTest("Stutter: pass edges are faded (no step beyond the fade slope), and Repeat 50 starts a stutter at about half of the boundaries");
        {
            const auto x = sine(96000, 440.0, kSR, 0.5f);
            mu_audio::StutterInsert fx;
            fx.prepare(kSR, 512);
            const auto y = render(fx, params(Stutter, 60.0f, 100.0f, 2.0f, 100.0f), x);
            float step = 0.0f;
            for (size_t i = 1; i < y.size(); ++i) step = std::max(step, std::abs(y[i] - y[i - 1]));
            expect(step < 0.08f, "largest step " + juce::String(step, 4));

            mu_audio::StutterInsert dice;
            dice.prepare(kSR, 512);
            dice.setSlotIndex(3);
            render(dice, params(Stutter, 5.0f, 50.0f, 0.0f, 100.0f), noise(1500000, 35));
            const double rate = (double) dice.hitCount() / (double) std::max(1, dice.rollCount());
            logMessage("Stutter Repeat 50: " + juce::String(dice.hitCount()) + " of " + juce::String(dice.rollCount()) + " boundaries");
            expect(dice.rollCount() > 1500 && std::abs(rate - 0.5) < 0.05, "hit rate " + juce::String(rate, 3) + " over " + juce::String(dice.rollCount()) + " boundaries");
        }

        // ── Lo-Fi ────────────────────────────────────────────────────────────────────────────────────────────────────
        beginTest("Lo-Fi: all amounts at 0 with Tone open is untouched, Wobble 100 delays by at most 2.5 ms, a 1 kHz tone swings 0.3 to 1.2 % in pitch");
        {
            const auto x = noise(30000, 41);
            mu_audio::LoFiInsert fx;
            fx.prepare(kSR, 512);
            expect(render(fx, params(LoFi, 0.0f, 0.0f, 0.0f, 20000.0f), x) == x, "bit-exact dry");

            mu_audio::LoFiInsert lat;
            lat.prepare(kSR, 512);
            const auto h = impulse(lat, params(LoFi, 100.0f, 0.0f, 0.0f, 20000.0f), 1000);
            int at = 0;
            for (int i = 1; i < (int) h.size(); ++i) if (std::abs(h[(size_t) i]) > std::abs(h[(size_t) at])) at = i;
            logMessage("Lo-Fi impulse peak at sample " + juce::String(at));
            expect(at <= (int) std::ceil(0.0025 * kSR), "peak at " + juce::String(at) + " samples");

            mu_audio::LoFiInsert wob;
            wob.prepare(kSR, 512);
            const int n = 20 * 48000;
            const auto y = render(wob, params(LoFi, 100.0f, 0.0f, 0.0f, 20000.0f), sine(n, 1000.0, kSR, 0.5f));
            // The pitch of every cycle from the spacing of its rising zero crossings (linear interpolation).
            double prev = -1.0, worst = 0.0;
            for (int i = 48000; i < n - 1; ++i)
                if (y[(size_t) i] <= 0.0f && y[(size_t) i + 1] > 0.0f)
                {
                    const double t = (double) i + (double) (-y[(size_t) i]) / (double) (y[(size_t) i + 1] - y[(size_t) i]);
                    if (prev >= 0.0) worst = std::max(worst, std::abs(kSR / (t - prev) / 1000.0 - 1.0));
                    prev = t;
                }
            logMessage("Lo-Fi Wobble 100: largest pitch deviation " + juce::String(worst * 100.0, 2) + " %");
            expect(worst > 0.003 && worst < 0.012, "pitch deviation " + juce::String(worst * 100.0, 2) + " %");
        }

        beginTest("Lo-Fi: hiss is about -35 dBFS at Noise 100, silence stays exactly silent at 0, crackle fires 25 to 55 pops a second at Crackle 100");
        {
            const Wave silence(5 * 48000, 0.0f);
            mu_audio::LoFiInsert hiss;
            hiss.prepare(kSR, 512);
            const auto y = render(hiss, params(LoFi, 0.0f, 100.0f, 0.0f, 20000.0f), silence);
            const double level = 20.0 * std::log10(rms(y, 0, (int) y.size()));
            logMessage("Lo-Fi Noise 100: " + juce::String(level, 2) + " dBFS RMS");
            expect(std::abs(level + 34.0) < 2.0, "hiss level " + juce::String(level, 2) + " dBFS");

            mu_audio::LoFiInsert zero;
            zero.prepare(kSR, 512);
            const auto z = render(zero, params(LoFi, 60.0f, 0.0f, 0.0f, 3000.0f), silence);
            bool exact = true;
            for (float v : z) exact = exact && v == 0.0f;
            expect(exact, "no Noise and no Crackle on silence is exact zero");

            mu_audio::LoFiInsert pops;
            pops.prepare(kSR, 512);
            const int seconds = 20;
            const auto c = render(pops, params(LoFi, 0.0f, 0.0f, 100.0f, 20000.0f), Wave((size_t) seconds * 48000, 0.0f));
            int count = 0;
            for (size_t i = 1; i < c.size(); ++i)
                if (std::abs(c[i]) > 0.03f && std::abs(c[i - 1]) <= 0.03f) ++count;
            logMessage("Lo-Fi Crackle 100: " + juce::String(count / (double) seconds, 1) + " pops a second");
            expect(count >= 25 * seconds && count <= 55 * seconds, juce::String(count / (double) seconds, 1) + " pops a second");
        }

        beginTest("Lo-Fi: Tone 1 kHz cuts 8 kHz by 20 dB and keeps 100 Hz, blocks of 1, 7 and 512 agree, slot index sets the noise, reset equals fresh, the peak stays below 2");
        {
            mu_audio::LoFiInsert tone;
            tone.prepare(kSR, 512);
            const int n = 48000;
            const auto lo = render(tone, params(LoFi, 0.0f, 0.0f, 0.0f, 1000.0f), sine(n, 100.0, kSR, 0.5f));
            mu_audio::LoFiInsert tone2;
            tone2.prepare(kSR, 512);
            const auto hi = render(tone2, params(LoFi, 0.0f, 0.0f, 0.0f, 1000.0f), sine(n, 8000.0, kSR, 0.5f));
            expect(std::abs(db(mag(lo, 100.0, kSR, 24000, 24000) / 0.5)) < 0.5, "100 Hz kept");
            expect(db(mag(hi, 8000.0, kSR, 24000, 24000) / 0.5) < -20.0, "8 kHz cut by " + juce::String(db(mag(hi, 8000.0, kSR, 24000, 24000) / 0.5), 1) + " dB");

            const auto p = params(LoFi, 50.0f, 30.0f, 30.0f, 4000.0f);
            const auto x = noise(20000, 42);
            Wave ref;
            for (int block : { 512, 7, 1 })
            {
                mu_audio::LoFiInsert fx;
                fx.prepare(kSR, 512);
                fx.setSlotIndex(2);
                const auto y = render(fx, p, x, block);
                if (ref.empty()) ref = y;
                float worst = 0.0f;
                for (size_t i = 0; i < y.size(); ++i) worst = std::max(worst, std::abs(y[i] - ref[i]));
                expect(worst < 1.0e-5f, "block " + juce::String(block) + " differs from block 512 by " + juce::String(worst));
            }
            mu_audio::LoFiInsert a, b, c;
            a.prepare(kSR, 512); b.prepare(kSR, 512); c.prepare(kSR, 512);
            a.setSlotIndex(1); b.setSlotIndex(2); c.setSlotIndex(1);
            const auto ya = render(a, p, x), yb = render(b, p, x), yc = render(c, p, x);
            expect(ya != yb, "two slots make different noise");
            expect(ya == yc, "the same slot makes the same noise");
            a.reset();
            expect(render(a, p, x) == yc, "reset equals fresh");

            mu_audio::LoFiInsert loud;
            loud.prepare(kSR, 512);
            const auto big = render(loud, params(LoFi, 100.0f, 100.0f, 100.0f, 20000.0f), Wave(96000, 1.0f));
            expect(allFinite(big) && peakOf(big) < 2.0f, "peak " + juce::String(peakOf(big), 3));
        }

        // ── All five ─────────────────────────────────────────────────────────────────────────────────────────────────
        auto make = [](int algo) -> std::unique_ptr<InsertAlgorithmBase>
        {
            switch (algo)
            {
                case Comb:      return std::make_unique<mu_audio::CombInsert>();
                case FreqShift: return std::make_unique<mu_audio::FreqShiftInsert>();
                case Formant:   return std::make_unique<mu_audio::FormantInsert>();
                case Stutter:   return std::make_unique<mu_audio::StutterInsert>();
                default:        return std::make_unique<mu_audio::LoFiInsert>();
            }
        };

        beginTest("All five: finite and bounded at every corner, at 44.1, 96 and 192 kHz and block sizes 1, 7 and 512");
        {
            const float corner[2] = { 0.0f, 1.0f };
            for (int algo = Comb; algo <= LoFi; ++algo)
                for (double sr : { 44100.0, 96000.0, 192000.0 })
                    for (int block : { 1, 7, 512 })
                        for (int mask = 0; mask < 16; ++mask)
                            for (int sync = 0; sync < 2; ++sync)
                            {
                                if (sync && algo != Stutter) continue;
                                if (block == 1 && mask % 5 != 0) continue;   // block 1 is slow: a subset of corners
                                VoiceParams p;
                                p.insertAlgo = algo;
                                for (int s = 0; s < 4; ++s) p.insertParam[s] = corner[(mask >> s) & 1];
                                p.insertSync = sync != 0;
                                auto fx = make(algo);
                                fx->prepare(sr, 512);
                                const int len = block == 1 ? 3000 : (int) (0.4 * sr);
                                const auto y = render(*fx, p, noise(len, 5), block);
                                const float pk = peakOf(y);
                                expect(allFinite(y) && pk < 8.0f, juce::String(mu_audio::kInsertAlgorithmNames[algo]) + " corner " + juce::String(mask)
                                       + " at " + juce::String(sr, 0) + " Hz block " + juce::String(block) + " peak " + juce::String(pk));
                            }
        }

        beginTest("All five: a NaN / Inf / huge burst leaves no latched state, and reset gives a render identical to a fresh instance");
        {
            for (int algo = Comb; algo <= LoFi; ++algo)
            {
                const auto p = params(algo,
                                      algo == Comb ? 440.0f : algo == FreqShift ? 80.0f : algo == Formant ? 40.0f : algo == Stutter ? 30.0f : 60.0f,
                                      algo == Comb ? 80.0f : algo == FreqShift ? 5.0f : algo == Formant ? 2.0f : algo == Stutter ? 80.0f : 20.0f,
                                      algo == Comb ? 30.0f : algo == FreqShift ? 40.0f : algo == Formant ? 50.0f : algo == Stutter ? 1.0f : 20.0f,
                                      algo == LoFi ? 5000.0f : 60.0f);
                const auto x = noise(30000, 9, 0.5f);
                auto fresh = make(algo);
                fresh->prepare(kSR, 512);
                const auto ref = render(*fresh, p, x);

                auto used = make(algo);
                used->prepare(kSR, 512);
                Wave bad(4000, 0.2f);
                bad[10] = std::numeric_limits<float>::quiet_NaN();
                bad[20] = std::numeric_limits<float>::infinity();
                bad[30] = -std::numeric_limits<float>::infinity();
                bad[40] = 1.0e30f;
                const auto burst = render(*used, p, bad);
                expect(allFinite(burst), juce::String(mu_audio::kInsertAlgorithmNames[algo]) + " output stayed finite through a NaN / Inf burst");
                used->reset();
                const auto again = render(*used, p, x);
                expect(again == ref, juce::String(mu_audio::kInsertAlgorithmNames[algo]) + " reset render differs from a fresh render");
            }
        }

        beginTest("All five: the slot table names the knobs, the defaults are in range, only Stutter syncs, and the names are the preset keys");
        {
            for (int algo = Comb; algo <= LoFi; ++algo)
                for (int s = 0; s < 4; ++s)
                {
                    const auto& c = mu_ui::kInsertAlgoSlots[algo][s];
                    expect(c.label != nullptr, "slot " + juce::String(s) + " has a label");
                    const float d = mu_ui::kInsertAlgoDefaults[algo][s];
                    expect(d >= c.minVal && d <= c.maxVal, juce::String(mu_audio::kInsertAlgorithmNames[algo]) + " default " + juce::String(s) + " in range");
                }
            for (int algo = Comb; algo <= LoFi; ++algo)
                expect(mu_ui::insertHasSync(algo) == (algo == Stutter), "only Stutter syncs");
            expectEquals(juce::String(mu_audio::kInsertAlgorithmNames[Comb]), juce::String("Comb"));
            expectEquals(juce::String(mu_audio::kInsertAlgorithmNames[FreqShift]), juce::String("FreqShift"));
            expectEquals(juce::String(mu_audio::kInsertAlgorithmNames[Formant]), juce::String("Formant"));
            expectEquals(juce::String(mu_audio::kInsertAlgorithmNames[Stutter]), juce::String("Stutter"));
            expectEquals(juce::String(mu_audio::kInsertAlgorithmNames[LoFi]), juce::String("LoFi"));
            expectEquals(juce::String(mu_ui::kInsertAlgoSlots[Stutter][2].enumNames[1]), juce::String("Rev"));
        }
    }
};

static FiveInsertsTests sFiveInsertsTests;
