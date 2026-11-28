#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Audio/AlgorithmNames.h"
#include "Audio/FX/Algorithms/AutoPanInsert.h"
#include "Audio/FX/Algorithms/GateInsert.h"
#include "Audio/FX/Algorithms/WaveshaperInsert.h"
#include "Audio/FX/Algorithms/WaveshaperCurve.h"
#include "Audio/FX/Algorithms/OverdriveInsert.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

// The Auto Pan (22), Gate (23) and Waveshaper (24) inserts: each is untouched where its depth / range is neutral, follows its knobs
// (constant-power pan, the gate's threshold, hysteresis, hold and times, the valve curve's even harmonics, bias and sag), stays finite
// at every rate and block size, and resets to a fresh state. All offline; nothing is listened to.
struct ThreeInsertsTests : juce::UnitTest
{
    ThreeInsertsTests() : juce::UnitTest("New inserts: auto pan, gate, waveshaper", "mu-core") {}

    static constexpr double kSR = 48000.0;
    static constexpr int kSettle = 8192;
    enum { AutoPan = 22, Gate = 23, Waveshaper = 24 };

    // Knob values in their real units (Hz, dB, %), stored the way a preset stores them (normalised).
    static VoiceParams params(int algo, float a0, float a1, float a2, float a3)
    {
        VoiceParams p;
        p.insertAlgo = algo;
        const float v[4] = { a0, a1, a2, a3 };
        for (int s = 0; s < 4; ++s) p.insertParam[s] = mu_ui::actualToNorm(v[s], algo, s);
        return p;
    }

    using Wave = std::vector<float>;
    struct Stereo { Wave l, r; };

    static Wave sine(int n, double f, double sr, float amp)
    {
        Wave v((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = amp * (float) std::sin(2.0 * juce::MathConstants<double>::pi * f * (double) i / sr);
        return v;
    }
    static Wave constant(int n, float v) { return Wave((size_t) n, v); }
    static Wave noise(int n, unsigned seed, float amp = 0.8f)
    {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> d(-amp, amp);
        Wave v((size_t) n);
        for (auto& x : v) x = d(rng);
        return v;
    }

    // Runs a two-channel input through an insert in blocks of `block`.
    static Stereo render2(InsertAlgorithmBase& fx, const VoiceParams& p, const Wave& inL, const Wave& inR, int block = 512)
    {
        Stereo out { Wave(inL.size()), Wave(inL.size()) };
        juce::AudioBuffer<float> buf(2, block);
        for (size_t pos = 0; pos < inL.size(); pos += (size_t) block)
        {
            const int n = (int) std::min<size_t>((size_t) block, inL.size() - pos);
            for (int i = 0; i < n; ++i)
            {
                buf.setSample(0, i, inL[pos + (size_t) i]);
                buf.setSample(1, i, inR[pos + (size_t) i]);
            }
            float gr = 0.0f;
            fx.process(buf, n, 2, p, gr);
            for (int i = 0; i < n; ++i)
            {
                out.l[pos + (size_t) i] = buf.getSample(0, i);
                out.r[pos + (size_t) i] = buf.getSample(1, i);
            }
        }
        return out;
    }
    static Wave render(InsertAlgorithmBase& fx, const VoiceParams& p, const Wave& in, int block = 512)
    {
        return render2(fx, p, in, in, block).l;
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
    static float peakOf(const Wave& y, int start, int n)
    {
        float m = 0.0f;
        for (int i = 0; i < n; ++i) m = std::max(m, std::abs(y[(size_t) (start + i)]));
        return m;
    }
    static bool allFinite(const Wave& y) { for (float v : y) if (! std::isfinite(v)) return false; return true; }

    static Wave concat(const Wave& a, const Wave& b) { Wave r = a; r.insert(r.end(), b.begin(), b.end()); return r; }

    void runTest() override
    {
        // ── Auto Pan ─────────────────────────────────────────────────────────────────────────────────────────────────
        beginTest("Auto Pan: Depth 0 with Centre 0 leaves the signal untouched, and a mono layer passes through");
        {
            const auto x = noise(20000, 3);
            mu_audio::AutoPanInsert fx;
            fx.prepare(kSR, 512);
            const auto y = render(fx, params(AutoPan, 2.0f, 0.0f, 0.0f, 0.0f), x);
            float worst = 0.0f;
            for (size_t i = 0; i < x.size(); ++i) worst = std::max(worst, std::abs(y[i] - x[i]));
            expect(worst < 1.0e-5f, "Auto Pan at Depth 0 / Centre 0, worst error " + juce::String(worst));

            mu_audio::AutoPanInsert mono;
            mono.prepare(kSR, 512);
            juce::AudioBuffer<float> buf(1, 512);
            for (int i = 0; i < 512; ++i) buf.setSample(0, i, x[(size_t) i]);
            float gr = 0.0f;
            mono.process(buf, 512, 1, params(AutoPan, 2.0f, 100.0f, 0.0f, 80.0f), gr);
            float worstMono = 0.0f;
            for (int i = 0; i < 512; ++i) worstMono = std::max(worstMono, std::abs(buf.getSample(0, i) - x[(size_t) i]));
            expect(worstMono == 0.0f, "a mono layer is unchanged");
        }

        beginTest("Auto Pan: constant power (gL^2 + gR^2 = 2) across the Centre range, and full right puts the left silent");
        {
            for (float centre = -100.0f; centre <= 100.0f; centre += 12.5f)
            {
                mu_audio::AutoPanInsert fx;
                fx.prepare(kSR, 512);
                const auto o = render2(fx, params(AutoPan, 2.0f, 0.0f, 0.0f, centre), constant(1024, 1.0f), constant(1024, 1.0f));
                const float gL = o.l.back(), gR = o.r.back();
                expect(std::abs(gL * gL + gR * gR - 2.0f) < 1.0e-4f, "power at Centre " + juce::String(centre, 1) + " is " + juce::String(gL * gL + gR * gR, 5));
            }
            mu_audio::AutoPanInsert fx;
            fx.prepare(kSR, 512);
            const auto o = render2(fx, params(AutoPan, 2.0f, 0.0f, 0.0f, 100.0f), constant(1024, 1.0f), constant(1024, 1.0f));
            expect(std::abs(o.l.back()) < 1.0e-6f, "Centre +100 leaves the left silent (" + juce::String(o.l.back(), 8) + ")");
            expect(std::abs(o.r.back() - std::sqrt(2.0f)) < 1.0e-5f, "Centre +100 puts sqrt(2) on the right");
        }

        beginTest("Auto Pan: left and right swing in opposition over one period of a 2 Hz sweep");
        {
            mu_audio::AutoPanInsert fx;
            fx.prepare(kSR, 512);
            const auto o = render2(fx, params(AutoPan, 2.0f, 100.0f, 0.0f, 0.0f), constant(48000, 1.0f), constant(48000, 1.0f));
            const size_t q1 = (size_t) (0.125 * kSR), q3 = (size_t) (0.375 * kSR);   // sine at its top and its bottom
            expect(o.l[q1] < 0.02f && o.r[q1] > 1.3f, "a quarter period in: right " + juce::String(o.r[q1], 3) + ", left " + juce::String(o.l[q1], 3));
            expect(o.r[q3] < 0.02f && o.l[q3] > 1.3f, "three quarters in: left " + juce::String(o.l[q3], 3) + ", right " + juce::String(o.r[q3], 3));
        }

        beginTest("Auto Pan: the square shape has no step between samples above a small bound");
        {
            mu_audio::AutoPanInsert fx;
            fx.prepare(kSR, 512);
            const auto o = render2(fx, params(AutoPan, 3.0f, 100.0f, 2.0f, 0.0f), constant(48000, 1.0f), constant(48000, 1.0f));
            float maxStep = 0.0f;
            for (size_t i = 1; i < o.l.size(); ++i) maxStep = std::max(maxStep, std::abs(o.l[i] - o.l[i - 1]));
            expect(maxStep < 0.05f, "largest sample-to-sample step " + juce::String(maxStep, 4));
        }

        beginTest("Auto Pan: with Sync four sixteenths at 120 bpm is a 2 Hz sweep");
        {
            mu_audio::AutoPanInsert fx;
            fx.prepare(kSR, 512);
            fx.tempoBpm = 120.0;
            auto p = params(AutoPan, 1.0f, 100.0f, 0.0f, 0.0f);
            p.insertParam[0] = mu_ui::rateSyncSixteenthsToNorm(4.0);
            p.insertSync = true;
            const auto o = render2(fx, p, constant(96000, 1.0f), constant(96000, 1.0f));
            int up = 0;
            for (size_t i = 1; i < o.l.size(); ++i)
                if (o.r[i - 1] - o.l[i - 1] <= 0.0f && o.r[i] - o.l[i] > 0.0f) ++up;
            expect(up >= 3 && up <= 4, "rising crossings in 2 s: " + juce::String(up));
        }

        // ── Gate ────────────────────────────────────────────────────────────────────────────────────────────────────
        beginTest("Gate: a tone 20 dB above the Threshold passes, one 20 dB below is cut by Range, Range 0 is untouched");
        {
            const int n = 96000;
            {
                mu_audio::GateInsert fx;
                fx.prepare(kSR, 512);
                const auto x = sine(n, 1000.0, kSR, 0.1f);   // -20 dBFS, Threshold -40
                const auto y = render(fx, params(Gate, -40.0f, -30.0f, 1.0f, 50.0f), x);
                expect(std::abs(db(rms(y, kSettle, 48000) / rms(x, kSettle, 48000))) < 0.1, "a loud tone is not changed");
            }
            {
                mu_audio::GateInsert fx;
                fx.prepare(kSR, 512);
                const auto x = sine(n, 1000.0, kSR, 0.001f);   // -60 dBFS
                const auto y = render(fx, params(Gate, -40.0f, -30.0f, 1.0f, 50.0f), x);
                const double cut = db(rms(y, 48000, 48000) / rms(x, 48000, 48000));
                expect(std::abs(cut + 30.0) < 0.5, "a quiet tone is cut by " + juce::String(cut, 2) + " dB (Range -30)");
            }
            {
                mu_audio::GateInsert fx;
                fx.prepare(kSR, 512);
                const auto x = sine(n, 1000.0, kSR, 0.001f);
                const auto y = render(fx, params(Gate, -40.0f, 0.0f, 1.0f, 50.0f), x);
                const size_t la = (size_t) fx.lookAheadSamples();
                float worst = 0.0f;
                for (size_t i = 0; i + la < x.size(); ++i) worst = std::max(worst, std::abs(y[i + la] - x[i]));
                expect(worst == 0.0f, "Range 0 leaves the signal bit-exact (only delayed by the look-ahead)");
            }
        }

        beginTest("Gate: it publishes its input level on the Threshold knob's own -80..0 dB scale (the knob ring's meter)");
        {
            mu_audio::GateInsert fx;
            fx.prepare(kSR, 512);
            const auto vp = params(Gate, -40.0f, -30.0f, 1.0f, 50.0f);
            juce::AudioBuffer<float> buf(2, 512);
            for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 512; ++i) buf.setSample(ch, i, 0.1f);   // -20 dBFS
            float packed = -1.0f, level = -1.0f, gr = -1.0f;
            fx.process(buf, 512, 2, vp, packed);
            mu_ui::unpackLevelAndGR(packed, level, gr);
            expectWithinAbsoluteError(level, 0.75f, 0.01f, "-20 dB is three quarters of the -80..0 sweep");
            expect(gr < 0.01f, "an open gate shows no gain reduction");

            buf.clear();
            fx.process(buf, 512, 2, vp, packed);
            mu_ui::unpackLevelAndGR(packed, level, gr);
            expect(level < 0.01f, "silence reads as the bottom of the sweep");

            // Well past the hold and release, the closed gate cuts by its Range (-30 dB = 30/80 of the sweep).
            for (int b = 0; b < 40; ++b) { buf.clear(); fx.process(buf, 512, 2, vp, packed); }
            mu_ui::unpackLevelAndGR(packed, level, gr);
            expectWithinAbsoluteError(gr, 30.0f / 80.0f, 0.01f, "the closed gate's gain reduction is its Range");
        }

        beginTest("Gate meter packing: two 0..1 values survive one float exactly to 12 bits");
        {
            for (float a : { 0.0f, 0.25f, 0.5f, 1.0f })
                for (float b : { 0.0f, 0.375f, 0.9f, 1.0f })
                {
                    float x = -1.0f, y = -1.0f;
                    mu_ui::unpackLevelAndGR(mu_ui::packLevelAndGR(a, b), x, y);
                    expectWithinAbsoluteError(x, a, 1.0f / 4095.0f);
                    expectWithinAbsoluteError(y, b, 1.0f / 4095.0f);
                }
        }

        beginTest("Gate: Range -80 gives exact zeros after the release");
        {
            mu_audio::GateInsert fx;
            fx.prepare(kSR, 512);
            const auto y = render(fx, params(Gate, -40.0f, -80.0f, 1.0f, 50.0f), sine(96000, 1000.0, kSR, 0.001f));
            bool zeros = true;
            for (size_t i = y.size() - 4096; i < y.size(); ++i) zeros = zeros && y[i] == 0.0f;
            expect(zeros, "the closed gate is not exactly silent");
        }

        beginTest("Gate: a level between the open and close thresholds does not chatter, open or closed");
        {
            const float amp = 0.01f * std::pow(10.0f, -2.0f / 20.0f);   // Threshold -40 dB, 2 dB below it: inside the 4 dB band
            {
                mu_audio::GateInsert fx;   // starts open, stays open
                fx.prepare(kSR, 512);
                const auto x = sine(96000, 1000.0, kSR, amp);
                const auto y = render(fx, params(Gate, -40.0f, -60.0f, 1.0f, 50.0f), x);
                expect(std::abs(db(rms(y, 48000, 48000) / rms(x, 48000, 48000))) < 0.2, "an open gate stays open inside the hysteresis band");
            }
            {
                mu_audio::GateInsert fx;   // closed by silence, stays closed
                fx.prepare(kSR, 512);
                const auto x = concat(constant(48000, 0.0f), sine(96000, 1000.0, kSR, amp));
                const auto y = render(fx, params(Gate, -40.0f, -60.0f, 1.0f, 50.0f), x);
                const double g = db(rms(y, 48000 + 48000, 48000) / rms(x, 48000 + 48000, 48000));
                expect(std::abs(g + 60.0) < 0.5, "a closed gate stays closed inside the band (" + juce::String(g, 2) + " dB)");
            }
        }

        beginTest("Gate: Hold keeps it open about 20 ms after the level drops, then it closes");
        {
            mu_audio::GateInsert fx;
            fx.prepare(kSR, 512);
            const int on = 48000;
            const auto x = concat(sine(on, 1000.0, kSR, 0.5f), sine(24000, 1000.0, kSR, 0.002f));   // 0.002 is -54 dB: below the close threshold
            const auto y = render(fx, params(Gate, -40.0f, -80.0f, 1.0f, 5.0f), x);
            const int w = 48;   // one cycle of the tone
            const float soon = peakOf(y, on + 480, w);           // 10 ms after the drop: held open
            const float late = peakOf(y, on + 24000 - w, w);     // 0.5 s after: closed
            expect(soon > 0.0015f, "the gate closed within the hold (" + juce::String(soon, 5) + ")");
            expect(late < 1.0e-5f, "the gate never closed (" + juce::String(late, 6) + ")");
        }

        beginTest("Gate: attack and release times are within 20 % of the knobs");
        {
            mu_audio::GateInsert fx;
            fx.prepare(kSR, 512);
            // 1 s of a constant below the threshold (closes after the 20 ms hold, falls with the release), then a loud step.
            const auto x = concat(constant(48000, 0.001f), constant(24000, 0.5f));
            const auto y = render(fx, params(Gate, -40.0f, -80.0f, 5.0f, 100.0f), x);
            int fall = -1;
            for (int i = fx.lookAheadSamples(); i < 48000 && fall < 0; ++i)   // the output starts after the look-ahead
                if (y[(size_t) i] <= 0.001f * std::exp(-1.0f)) fall = i;
            const double releaseMs = (fall - (int) std::lround(0.020 * kSR) - 1) * 1000.0 / kSR;
            expect(std::abs(releaseMs - 100.0) < 20.0, "release time constant " + juce::String(releaseMs, 1) + " ms");
            int rise = -1;
            for (int i = 0; i < 24000 && rise < 0; ++i)
                if (y[(size_t) (48000 + i)] >= 0.5f * (1.0f - std::exp(-1.0f))) rise = i;
            const double attackMs = rise * 1000.0 / kSR;
            expect(std::abs(attackMs - 5.0) < 1.0, "attack time constant " + juce::String(attackMs, 2) + " ms");
        }

        beginTest("Gate: a hit that arrives while the gate is releasing plays at full level from its first sample (look-ahead)");
        {
            mu_audio::GateInsert fx;
            fx.prepare(kSR, 512);
            // A loud burst, a quiet gap long enough to close and half-release, then a second loud burst.
            const auto x = concat(concat(sine(9600, 1000.0, kSR, 0.5f), constant(9600, 0.0f)), sine(9600, 1000.0, kSR, 0.5f));
            const auto y = render(fx, params(Gate, -40.0f, -60.0f, 1.0f, 150.0f), x);
            const int la = fx.lookAheadSamples();
            const int hit = 19200;
            const float early = peakOf(y, hit + la + 24, 24) / peakOf(x, hit + 24, 24);   // the hit's second half-millisecond
            expect(early > 0.85f, "the hit started at " + juce::String(early, 3) + " of its level");
        }

        beginTest("Gate: one gain serves both channels (a quiet right channel is cut like the left)");
        {
            mu_audio::GateInsert fx;
            fx.prepare(kSR, 512);
            const auto o = render2(fx, params(Gate, -40.0f, -40.0f, 1.0f, 100.0f), constant(48000, 0.0002f), constant(48000, 0.0005f));
            float lowest = 1.0f, worst = 0.0f;
            for (size_t i = 0; i < o.l.size(); ++i)
            {
                const float gl = o.l[i] / 0.0002f, gr = o.r[i] / 0.0005f;
                worst = std::max(worst, std::abs(gl - gr));
                lowest = std::min(lowest, gl);
            }
            expect(lowest < 0.5f, "the gate did close during the run");
            expect(worst < 1.0e-4f, "left and right gains differ by " + juce::String(worst, 6));
        }

        // ── Waveshaper ──────────────────────────────────────────────────────────────────────────────────────────────
        beginTest("Waveshaper curve: slope 1 at 0, continuous across 0, reaches its limits, asymmetric");
        {
            using namespace mu_audio::waveshaper;
            expect(std::abs((curve(1.0e-3f) - curve(-1.0e-3f)) / 2.0e-3f - 1.0f) < 2.0e-3f, "slope at 0");
            expect(std::abs(curve(1.0e-6f)) < 2.0e-6f && std::abs(curve(-1.0e-6f)) < 2.0e-6f && curve(0.0f) == 0.0f, "continuous at 0");
            expect(std::abs(curve(1.0e6f) - kPositiveLimit) < 1.0e-3f, "positive limit");
            expect(std::abs(curve(-1.0e6f) - kNegativeLimit) < 1.0e-3f, "negative limit");
            expect(std::abs(curve(0.5f) + curve(-0.5f)) > 0.01f, "not odd-symmetric");
        }

        beginTest("Waveshaper: even harmonics above Overdrive at Bias 0, growing with Bias, odd ones still present");
        {
            const int n = (int) kSR;
            const auto x = sine(n + kSettle, 1000.0, kSR, 0.25f);
            auto h2At = [&](float bias, double* h3Out = nullptr)
            {
                mu_audio::WaveshaperInsert fx;
                fx.prepare(kSR, 512);
                const auto y = render(fx, params(Waveshaper, 60.0f, 0.0f, bias, 0.0f), x);
                const double h1 = mag(y, 1000.0, kSR, kSettle, n);
                if (h3Out) *h3Out = db(mag(y, 3000.0, kSR, kSettle, n) / h1);
                return db(mag(y, 2000.0, kSR, kSettle, n) / h1);
            };
            double h3 = 0.0;
            const double w0 = h2At(0.0f, &h3), w30 = h2At(30.0f), w60 = h2At(60.0f);
            OverdriveInsert od;
            od.prepare(kSR, 512);
            auto op = params(14, 60.0f, 0.0f, 0.0f, 20000.0f);
            const auto yo = render(od, op, x);
            const double o2 = db(mag(yo, 2000.0, kSR, kSettle, n) / mag(yo, 1000.0, kSR, kSettle, n));
            logMessage("Waveshaper H2 at Bias 0/30/60: " + juce::String(w0, 1) + " / " + juce::String(w30, 1) + " / " + juce::String(w60, 1)
                       + " dB; H3 " + juce::String(h3, 1) + " dB; Overdrive Bias 0 H2 " + juce::String(o2, 1) + " dB");
            expect(w0 > o2 + 20.0, "H2 at Bias 0 is not above Overdrive");
            expect(w0 > -40.0, "H2 at Bias 0 is barely there");
            expect(w30 > w0 && w60 > w30, "H2 does not grow with Bias");
            expect(h3 > -60.0, "odd harmonics have vanished");
        }

        beginTest("Waveshaper: Sag pulls a loud burst's gain down and it recovers; Sag 0 does not depend on the level");
        {
            auto burstThenRest = [&](float sag)
            {
                mu_audio::WaveshaperInsert fx;
                fx.prepare(kSR, 512);
                // 1 s burst, 1.5 s of silence, the burst again.
                const auto b = sine(48000, 1000.0, kSR, 0.3f);
                const auto x = concat(concat(b, constant(72000, 0.0f)), b);
                return render(fx, params(Waveshaper, 60.0f, 0.0f, 0.0f, sag), x);
            };
            const auto sagged = burstThenRest(100.0f);
            const auto flat = burstThenRest(0.0f);
            const float early = peakOf(sagged, 96, 96), late = peakOf(sagged, 46000, 960), again = peakOf(sagged, 120000 + 96, 96);
            logMessage("Sag 100: first 2 ms " + juce::String(early, 3) + ", after 1 s " + juce::String(late, 3) + ", second burst " + juce::String(again, 3));
            expect(db(late / early) < -1.5, "the gain did not sag (" + juce::String(db(late / early), 2) + " dB)");
            expect(std::abs(db(again / early)) < 1.0, "the gain did not recover (" + juce::String(db(again / early), 2) + " dB)");
            const float fe = peakOf(flat, 2400, 960), fl = peakOf(flat, 46000, 960);
            expect(std::abs(db(fl / fe)) < 0.5, "Sag 0 changed the level over the burst (" + juce::String(db(fl / fe), 2) + " dB)");
        }

        beginTest("Waveshaper: no DC left after silence, at full Bias");
        {
            mu_audio::WaveshaperInsert fx;
            fx.prepare(kSR, 512);
            const auto x = concat(sine(14400, 200.0, kSR, 0.4f), constant(96000, 0.0f));
            const auto y = render(fx, params(Waveshaper, 80.0f, 0.0f, 100.0f, 50.0f), x);
            expect(peakOf(y, (int) y.size() - 4800, 4800) < 1.0e-4f, "DC after silence " + juce::String(peakOf(y, (int) y.size() - 4800, 4800), 7));
        }

        beginTest("Waveshaper: at Drive 100 and 9 kHz it aliases less than plain tanh at the same gain");
        {
            constexpr double sr = 44100.0;
            const int n = (int) sr;
            const auto x = sine(n + kSettle, 9000.0, sr, 0.25f);
            mu_audio::WaveshaperInsert fx;
            fx.prepare(sr, 512);
            const auto y = render(fx, params(Waveshaper, 100.0f, 0.0f, 0.0f, 0.0f), x);
            Wave ref(x.size());
            const float g = std::pow(10.0f, 30.0f / 20.0f);   // Drive 100 = +30 dB
            for (size_t i = 0; i < x.size(); ++i) ref[i] = std::tanh(g * x[i]);
            auto strongestAlias = [&](const Wave& s)
            {
                const double fund = mag(s, 9000.0, sr, kSettle, n);
                double worst = 0.0;
                for (int k = 3; k <= 9; ++k)
                {
                    const double fk = 9000.0 * (double) k;
                    const double folded = std::abs(fk - std::round(fk / sr) * sr);
                    if (std::abs(folded - 9000.0) < 1.0 || std::abs(folded - 18000.0) < 1.0) continue;
                    worst = std::max(worst, mag(s, folded, sr, kSettle, n));
                }
                return db(worst / fund);
            };
            const double aliasNew = strongestAlias(y), aliasRef = strongestAlias(ref);
            logMessage("Waveshaper strongest alias " + juce::String(aliasNew, 1) + " dB, plain tanh " + juce::String(aliasRef, 1) + " dB");
            expect(aliasNew <= aliasRef - 6.0, "the strongest alias is not at least 6 dB below plain tanh");
        }

        beginTest("Waveshaper: the impulse response peaks within 4 samples, and the level stays within 4.5 dB across Drive");
        {
            mu_audio::WaveshaperInsert fx;
            fx.prepare(kSR, 512);
            Wave imp(2048, 0.0f);
            imp[100] = 0.1f;
            const auto y = render(fx, params(Waveshaper, 0.0f, 0.0f, 0.0f, 0.0f), imp);
            int peakAt = 0; float best = 0.0f;
            for (int i = 0; i < 2048; ++i) if (std::abs(y[(size_t) i]) > best) { best = std::abs(y[(size_t) i]); peakAt = i; }
            expect(peakAt >= 100 && peakAt - 100 <= 4, "impulse peak " + juce::String(peakAt - 100) + " samples late");

            const int n = (int) kSR;
            const auto x = sine(n + kSettle, 1000.0, kSR, 0.25f);
            double base = 0.0;
            for (float d : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f })
            {
                mu_audio::WaveshaperInsert w;
                w.prepare(kSR, 512);
                const double level = rms(render(w, params(Waveshaper, d, 0.0f, 30.0f, 30.0f), x), kSettle, n);
                if (d == 0.0f) base = level;
                logMessage("Waveshaper Drive " + juce::String(d, 0) + ": " + juce::String(db(level / base), 2) + " dB");
                expect(std::abs(db(level / base)) <= 4.5, "Drive " + juce::String(d, 0) + " is " + juce::String(db(level / base), 2) + " dB from Drive 0");
            }
        }

        // ── All three ──────────────────────────────────────────────────────────────────────────────────────────────
        beginTest("All three: finite and bounded at every corner, at 44.1, 96 and 192 kHz and block sizes 1, 7 and 512");
        {
            const float corner[2] = { 0.0f, 1.0f };
            for (int algo : { (int) AutoPan, (int) Gate, (int) Waveshaper })
                for (double sr : { 44100.0, 96000.0, 192000.0 })
                    for (int block : { 1, 7, 512 })
                        for (int mask = 0; mask < 16; ++mask)
                            for (int sync = 0; sync < 2; ++sync)
                            {
                                if (sync && algo != AutoPan) continue;
                                if (block == 1 && mask % 5 != 0) continue;   // block 1 is slow: a subset of corners
                                VoiceParams p;
                                p.insertAlgo = algo;
                                for (int s = 0; s < 4; ++s) p.insertParam[s] = corner[(mask >> s) & 1];
                                p.insertSync = sync != 0;
                                std::unique_ptr<InsertAlgorithmBase> fx;
                                if (algo == AutoPan)         fx = std::make_unique<mu_audio::AutoPanInsert>();
                                else if (algo == Gate)       fx = std::make_unique<mu_audio::GateInsert>();
                                else                         fx = std::make_unique<mu_audio::WaveshaperInsert>();
                                fx->prepare(sr, 512);
                                const int len = block == 1 ? 3000 : (int) (0.4 * sr);
                                const auto y = render(*fx, p, noise(len, 5), block);
                                float pk = peakOf(y, 0, len);
                                expect(allFinite(y) && pk < 8.0f, juce::String(mu_audio::kInsertAlgorithmNames[algo]) + " corner " + juce::String(mask)
                                       + " at " + juce::String(sr, 0) + " Hz block " + juce::String(block) + " peak " + juce::String(pk));
                            }
        }

        beginTest("All three: a NaN / Inf / huge burst leaves no latched state, and reset gives a render identical to a fresh instance");
        {
            for (int algo : { (int) AutoPan, (int) Gate, (int) Waveshaper })
            {
                auto make = [&]() -> std::unique_ptr<InsertAlgorithmBase>
                {
                    if (algo == AutoPan) return std::make_unique<mu_audio::AutoPanInsert>();
                    if (algo == Gate)    return std::make_unique<mu_audio::GateInsert>();
                    return std::make_unique<mu_audio::WaveshaperInsert>();
                };
                const auto p = params(algo, algo == Gate ? -40.0f : 2.0f, algo == Gate ? -60.0f : 60.0f, algo == Waveshaper ? 40.0f : 1.0f, algo == Gate ? 100.0f : 30.0f);
                const auto x = noise(30000, 9, 0.5f);

                auto fresh = make();
                fresh->prepare(kSR, 512);
                const auto ref = render(*fresh, p, x);

                auto used = make();
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

        beginTest("All three: the slot table names the knobs and the defaults are in range");
        {
            for (int algo : { (int) AutoPan, (int) Gate, (int) Waveshaper })
                for (int s = 0; s < 4; ++s)
                {
                    const auto& c = mu_ui::kInsertAlgoSlots[algo][s];
                    expect(c.label != nullptr, "slot " + juce::String(s) + " has a label");
                    const float d = mu_ui::kInsertAlgoDefaults[algo][s];
                    expect(d >= c.minVal && d <= c.maxVal, juce::String(mu_audio::kInsertAlgorithmNames[algo]) + " default " + juce::String(s) + " in range");
                }
            expectEquals(juce::String(mu_ui::kInsertAlgoSlots[Waveshaper][3].label), juce::String("Sag"));
            expect(mu_ui::insertHasSync(AutoPan) && ! mu_ui::insertHasSync(Gate) && ! mu_ui::insertHasSync(Waveshaper), "only Auto Pan syncs");
        }
    }
};

static ThreeInsertsTests sThreeInsertsTests;
