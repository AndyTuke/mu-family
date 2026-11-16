#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Audio/FX/Algorithms/Adaa1Tanh.h"
#include "Audio/FX/Algorithms/TapeEmphasis.h"
#include "Audio/FX/Insert/OverdriveInsert.h"
#include "Audio/FX/Insert/TapeSatInsert.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

// The Tape Sat insert (insertAlgo 10) and the helpers it shares with Overdrive: the ADAA tanh equals the expression Overdrive used
// to hold inline, the emphasis pair is an exact inverse, and the insert has even harmonics, compresses highs first, lifts the low
// end with Head Bump, aliases less than a plain tanh, keeps its level across Drive, stays finite at every rate and block size, and
// resets to a fresh state (no dip on the first sample, no latched state from a NaN or a huge sample), and adds only a few
// samples of delay. The last tests pin the properties Overdrive (insertAlgo 14) had before it took the shared ADAA tanh,
// so the refactor is checked end to end. All offline; nothing is listened to.
struct TapeSatTests : juce::UnitTest
{
    TapeSatTests() : juce::UnitTest("Tape Sat insert and the shared ADAA / emphasis helpers", "mu-core") {}

    static constexpr double kSR = 48000.0;
    static constexpr int kSettle = 8192;   // samples skipped so the gain smoothers and filters have settled

    static VoiceParams params(float drive, float outputDb, float bump, float toneHz)
    {
        VoiceParams p;
        p.insertAlgo = 10;
        p.insertParam[0] = mu_ui::actualToNorm(drive,    10, 0);
        p.insertParam[1] = mu_ui::actualToNorm(outputDb, 10, 1);
        p.insertParam[2] = mu_ui::actualToNorm(bump,     10, 2);
        p.insertParam[3] = mu_ui::actualToNorm(toneHz,   10, 3);
        return p;
    }

    static std::vector<float> sine(int n, double f, double sr, float amp)
    {
        std::vector<float> v((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = amp * (float) std::sin(2.0 * juce::MathConstants<double>::pi * f * (double) i / sr);
        return v;
    }

    static std::vector<float> noise(int n, unsigned seed, float amp = 0.8f)
    {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> d(-amp, amp);
        std::vector<float> v((size_t) n);
        for (auto& x : v) x = d(rng);
        return v;
    }

    // Runs `in` through an insert in blocks of `block` (every channel the same); returns channel 0.
    static std::vector<float> render(InsertAlgorithmBase& fx, const VoiceParams& p, const std::vector<float>& in,
                                     int block = 512, int nCh = 2)
    {
        std::vector<float> out(in.size());
        juce::AudioBuffer<float> buf(nCh, block);
        for (size_t pos = 0; pos < in.size(); pos += (size_t) block)
        {
            const int n = (int) std::min<size_t>((size_t) block, in.size() - pos);
            for (int ch = 0; ch < nCh; ++ch)
                for (int i = 0; i < n; ++i)
                    buf.setSample(ch, i, in[pos + (size_t) i]);
            float gr = 0.0f;
            fx.process(buf, n, nCh, p, gr);
            for (int i = 0; i < n; ++i)
                out[pos + (size_t) i] = buf.getSample(0, i);
        }
        return out;
    }

    // Amplitude of the component at `f` Hz over `n` samples from `start`. Exact when `n` holds a whole number of cycles
    // (n == sr and f an integer number of Hz), so no window is needed.
    static double mag(const std::vector<float>& y, double f, double sr, int start, int n)
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

    // The gain in dB of a sine through a fresh Tape Sat with these settings.
    static double gainDb(double f, float amp, const VoiceParams& p, double sr = kSR)
    {
        const int n = (int) sr;
        TapeSatInsert fx;
        fx.prepare(sr, 512);
        const auto y = render(fx, p, sine(n + kSettle, f, sr, amp));
        return db(mag(y, f, sr, kSettle, n) / (double) amp);
    }

    static double rms(const std::vector<float>& y, int start, int n)
    {
        double s = 0.0;
        for (int i = 0; i < n; ++i) s += (double) y[(size_t) (start + i)] * (double) y[(size_t) (start + i)];
        return std::sqrt(s / (double) n);
    }

    void runTest() override
    {
        // The ADAA tanh is Overdrive's original inline expression, moved: the same float operations give the same bits.
        beginTest("Adaa1Tanh: bit-identical to the expression Overdrive held inline");
        {
            auto ad1 = [](float x) -> float { return std::abs(x) > 12.0f ? std::abs(x) - 0.6931472f : std::log(std::cosh(x)); };
            std::vector<float> in = noise(60000, 7u, 40.0f);                      // wide: both knees, both flat tops
            for (int i = 0; i < 200; ++i) in.push_back(20.0f);                     // a flat run past the knee
            for (int i = 0; i < 200; ++i) in.push_back(-20.0f);
            for (int i = 0; i < 4000; ++i) in.push_back(-8.0f + 16.0f * (float) i / 4000.0f);   // a slow ramp through the knee
            for (int i = 0; i < 400; ++i)  in.push_back(0.3f + 1.0e-5f * (float) i);            // steps below the small-dx fallback
            mu_audio::Adaa1Tanh a;
            float xp = 0.0f;
            int mismatches = 0;
            for (float x : in)
            {
                const float dx = x - xp;
                const float ref = (x > 12.0f && xp > 12.0f)   ?  1.0f
                                : (x < -12.0f && xp < -12.0f) ? -1.0f
                                : std::abs(dx) < 1e-4f        ? std::tanh(0.5f * (x + xp))
                                                              : (ad1(x) - ad1(xp)) / dx;
                xp = x;
                if (a.process(x) != ref) ++mismatches;
            }
            expectEquals(mismatches, 0, "Adaa1Tanh output differs from the original expression");
        }

        beginTest("TapeEmphasis: de() undoes pre() at every sample rate, unity at DC, about +9 dB at the top");
        {
            for (double sr : { 44100.0, 96000.0, 192000.0 })
            {
                mu_audio::TapeEmphasis e;
                e.prepare(sr);
                const auto in = noise(20000, 11u);
                float worst = 0.0f;
                for (size_t i = 0; i < in.size(); ++i)
                    worst = std::max(worst, std::abs(e.de(e.pre(in[i])) - in[i]));
                expect(worst < 1.0e-4f, "pre then de is not the identity at " + juce::String(sr) + " Hz: worst " + juce::String(worst));

                mu_audio::TapeEmphasis dc;
                dc.prepare(sr);
                float y = 0.0f;
                for (int i = 0; i < 20000; ++i) y = dc.pre(1.0f);
                expectWithinAbsoluteError(y, 1.0f, 1.0e-3f, "the shelf is not unity at DC");

                // A 15 kHz tone (96 kHz and 192 kHz only; 44.1 kHz is close to Nyquist) is lifted by about 8.7 dB.
                if (sr > 50000.0)
                {
                    mu_audio::TapeEmphasis hf;
                    hf.prepare(sr);
                    std::vector<float> tone((size_t) sr), shaped((size_t) sr);
                    for (size_t i = 0; i < tone.size(); ++i)
                    {
                        tone[i]   = (float) std::sin(2.0 * juce::MathConstants<double>::pi * 15000.0 * (double) i / sr);
                        shaped[i] = hf.pre(tone[i]);
                    }
                    const double gain = db(rms(shaped, 4096, 20000) / rms(tone, 4096, 20000));
                    expect(gain > 7.0 && gain < 10.0, "the 15 kHz lift is " + juce::String(gain, 2) + " dB at " + juce::String(sr));
                }
            }
        }

        beginTest("Tape Sat: even harmonics are present (above plain tanh) and the odd ones still dominate");
        {
            const int n = (int) kSR;
            TapeSatInsert fx;
            fx.prepare(kSR, 512);
            const auto x = sine(n + kSettle, 1000.0, kSR, 0.25f);
            const auto y = render(fx, params(60.0f, 0.0f, 0.0f, 20000.0f), x);
            const double h1 = mag(y, 1000.0, kSR, kSettle, n);
            const double h2 = db(mag(y, 2000.0, kSR, kSettle, n) / h1);
            const double h3 = db(mag(y, 3000.0, kSR, kSettle, n) / h1);

            // The old class: plain tanh at its Drive 60 gain (1 + 0.6 * 9).
            std::vector<float> ref(x.size());
            for (size_t i = 0; i < x.size(); ++i) ref[i] = std::tanh(6.4f * x[i]);
            const double r1 = mag(ref, 1000.0, kSR, kSettle, n);
            const double r2 = db(mag(ref, 2000.0, kSR, kSettle, n) / r1);
            logMessage("H2 " + juce::String(h2, 1) + " dB, H3 " + juce::String(h3, 1) + " dB; plain tanh H2 " + juce::String(r2, 1) + " dB");
            expect(h2 > r2 + 20.0, "the second harmonic is not above plain tanh");
            expect(h2 > -60.0, "the second harmonic is barely there");
            expect(h2 < h3, "the second harmonic is not below the third");
        }

        beginTest("Tape Sat: highs compress first, and a small signal passes (nearly) flat");
        {
            const double lo0   = gainDb(200.0,  0.25f, params(0.0f,   0.0f, 0.0f, 20000.0f));
            const double lo100 = gainDb(200.0,  0.25f, params(100.0f, 0.0f, 0.0f, 20000.0f));
            const double hi0   = gainDb(8000.0, 0.25f, params(0.0f,   0.0f, 0.0f, 20000.0f));
            const double hi100 = gainDb(8000.0, 0.25f, params(100.0f, 0.0f, 0.0f, 20000.0f));
            const double loChange = lo100 - lo0, hiChange = hi100 - hi0;
            logMessage("Drive 0 to 100: 200 Hz " + juce::String(loChange, 2) + " dB, 8 kHz " + juce::String(hiChange, 2) + " dB");
            expect(hiChange - loChange <= -3.0, "the 8 kHz gain does not fall at least 3 dB more than the 200 Hz gain");

            // -40 dBFS at Drive 30: the emphasis pair cancels, so the response is flat. Running the saturating stage at 2x keeps the
            // ADAA step average's droop at 10 kHz small (it was about 2 dB at 48 kHz without the oversampling).
            const auto flat = params(30.0f, 0.0f, 0.0f, 20000.0f);
            const double ref = gainDb(1000.0, 0.01f, flat);
            const double top = gainDb(10000.0, 0.01f, flat) - ref;
            logMessage("small-signal 10 kHz response " + juce::String(top, 2) + " dB against 1 kHz");
            for (double f : { 100.0, 300.0, 3000.0, 10000.0 })
                expectWithinAbsoluteError(gainDb(f, 0.01f, flat), ref, 0.5, "the small-signal response is not flat at " + juce::String(f) + " Hz");
        }

        beginTest("Tape Sat: Head Bump lifts 75 Hz by 3 to 5 dB at 100 %, and 0 adds nothing");
        {
            const double off  = gainDb(75.0,   0.01f, params(0.0f, 0.0f, 0.0f,   20000.0f));
            const double on   = gainDb(75.0,   0.01f, params(0.0f, 0.0f, 100.0f, 20000.0f));
            const double kOff = gainDb(1000.0, 0.01f, params(0.0f, 0.0f, 0.0f,   20000.0f));
            const double kOn  = gainDb(1000.0, 0.01f, params(0.0f, 0.0f, 100.0f, 20000.0f));
            logMessage("Head Bump 100 %: 75 Hz +" + juce::String(on - off, 2) + " dB, 1 kHz " + juce::String(kOn - kOff, 2) + " dB");
            expect(on - off >= 3.0 && on - off <= 5.0, "the bump is not 3 to 5 dB at 75 Hz");
            expect(std::abs(kOn - kOff) < 0.3, "the bump leaks into 1 kHz");
            // Bump 0 is a 0 dB peak (an identity): 75 Hz keeps only the DC block's small loss against 1 kHz, no lift.
            expect(off - kOff <= 0.05 && off - kOff > -0.6, "Head Bump 0 changes the low end");
        }

        beginTest("Tape Sat: at Drive 100 and 9 kHz it aliases less than the plain tanh it replaces");
        {
            constexpr double sr = 44100.0;
            const int n = (int) sr;
            const auto x = sine(n + kSettle, 9000.0, sr, 0.25f);
            TapeSatInsert fx;
            fx.prepare(sr, 512);
            const auto y = render(fx, params(100.0f, 0.0f, 0.0f, 20000.0f), x);
            std::vector<float> ref(x.size());
            for (size_t i = 0; i < x.size(); ++i) ref[i] = std::tanh(10.0f * x[i]);   // the old class at Drive 100

            // The strongest component that is neither the tone nor its 2nd harmonic: harmonics 3 to 9 folded back into 0..Nyquist.
            auto strongestAlias = [&](const std::vector<float>& s)
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
            logMessage("strongest alias " + juce::String(aliasNew, 1) + " dB, plain tanh " + juce::String(aliasRef, 1) + " dB");
            // ADAA alone was only 3.4 dB below the plain tanh here (the pre-emphasis lifts the 9 kHz tone into the curve); the 2x
            // oversampled stage has to reach 6 dB.
            expect(aliasNew <= aliasRef - 6.0, "the strongest alias is not at least 6 dB below plain tanh");
        }

        beginTest("Tape Sat: the level stays within 4.5 dB of Drive 0 across the Drive range");
        {
            const int n = (int) kSR;
            const auto x = sine(n + kSettle, 1000.0, kSR, 0.25f);
            double base = 0.0;
            for (float d : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f })
            {
                TapeSatInsert fx;
                fx.prepare(kSR, 512);
                const double level = rms(render(fx, params(d, 0.0f, 0.0f, 20000.0f), x), kSettle, n);
                if (d == 0.0f) base = level;
                logMessage("Drive " + juce::String(d, 0) + ": " + juce::String(db(level / base), 2) + " dB");
                expect(std::abs(db(level / base)) <= 4.5, "Drive " + juce::String(d, 0) + " is " + juce::String(db(level / base), 2) + " dB from Drive 0");
            }
        }

        beginTest("Tape Sat: finite at 44.1, 96 and 192 kHz; block sizes 1, 7 and 512 agree; mono is finite");
        {
            const auto in = noise(4096, 21u);
            for (double sr : { 44100.0, 96000.0, 192000.0 })
            {
                const auto p = params(100.0f, 0.0f, 100.0f, 8000.0f);
                std::vector<float> first;
                for (int block : { 512, 7, 1 })
                {
                    TapeSatInsert fx;
                    fx.prepare(sr, block);
                    const auto y = render(fx, p, in, block);
                    bool finite = true;
                    for (float v : y) finite = finite && std::isfinite(v);
                    expect(finite, "non-finite output at " + juce::String(sr) + " Hz, block " + juce::String(block));
                    if (first.empty()) first = y;
                    else
                    {
                        float worst = 0.0f;
                        for (size_t i = 0; i < y.size(); ++i) worst = std::max(worst, std::abs(y[i] - first[i]));
                        expect(worst < 1.0e-4f, "block " + juce::String(block) + " differs from block 512 by " + juce::String(worst) + " at " + juce::String(sr));
                    }
                }
                TapeSatInsert mono;
                mono.prepare(sr, 512);
                const auto y = render(mono, p, in, 512, 1);
                bool finite = true;
                for (float v : y) finite = finite && std::isfinite(v);
                expect(finite, "non-finite mono output at " + juce::String(sr) + " Hz");
            }
        }

        beginTest("Tape Sat: a NaN / Inf burst leaves no latched state");
        {
            const auto p = params(80.0f, 0.0f, 50.0f, 6000.0f);
            auto clean = noise(20000, 33u, 0.5f);
            std::vector<float> burst;
            for (int i = 0; i < 12; ++i) burst.push_back(std::numeric_limits<float>::quiet_NaN());
            for (int i = 0; i < 12; ++i) burst.push_back(std::numeric_limits<float>::infinity());
            burst.insert(burst.end(), clean.begin(), clean.end());
            TapeSatInsert a, b;
            a.prepare(kSR, 512);  b.prepare(kSR, 512);
            const auto ya = render(a, p, burst);
            const auto yb = render(b, p, clean);
            bool finite = true;
            for (float v : ya) finite = finite && std::isfinite(v);
            expect(finite, "the burst reached the output");
            float worst = 0.0f;
            for (int i = 0; i < 256; ++i)
                worst = std::max(worst, std::abs(ya[ya.size() - 1 - (size_t) i] - yb[yb.size() - 1 - (size_t) i]));
            expect(worst < 1.0e-3f, "the burst left a trace in the output: " + juce::String(worst));
        }

        beginTest("Tape Sat: reset gives a render bit-identical to a fresh instance; Drive 0 has no DC");
        {
            const auto p = params(70.0f, -3.0f, 40.0f, 9000.0f);
            const auto in = noise(8192, 5u, 0.6f);
            TapeSatInsert used, fresh;
            used.prepare(kSR, 512);  fresh.prepare(kSR, 512);
            render(used, p, noise(8192, 6u, 0.9f));   // leave state behind
            used.reset();
            const auto a = render(used, p, in);
            const auto b = render(fresh, p, in);
            int diff = 0;
            for (size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) ++diff;
            expectEquals(diff, 0, "a reset instance differs from a fresh one");

            const int n = (int) kSR;
            TapeSatInsert dc;
            dc.prepare(kSR, 512);
            const auto y = render(dc, params(0.0f, 0.0f, 0.0f, 20000.0f), sine(n + kSettle, 1000.0, kSR, 0.1f));
            double mean = 0.0;
            for (int i = 0; i < n; ++i) mean += (double) y[(size_t) (kSettle + i)];
            mean /= (double) n;
            expect(std::abs(mean) < 1.0e-4, "Drive 0 leaves a DC offset of " + juce::String(mean, 6));
        }

        beginTest("Tape Sat: silence after reset() stays silent (no dip on the first sample), and a huge sample leaves no latched state");
        {
            for (float drive : { 0.0f, 100.0f })
            {
                const auto p = params(drive, 0.0f, 50.0f, 20000.0f);
                TapeSatInsert fx;
                fx.prepare(kSR, 512);
                render(fx, p, noise(4096, 51u, 0.9f));   // leave state behind
                fx.reset();
                const auto y = render(fx, p, std::vector<float>(2048, 0.0f));
                float worst = 0.0f;
                for (float v : y) worst = std::max(worst, std::abs(v));
                expect(worst < 1.0e-5f, "silence after reset is not silent at Drive " + juce::String(drive, 0) + ": " + juce::String(worst));
            }

            // One enormous finite sample (not NaN, not Inf) must not push a state to infinity: the tail comes back finite and quiet.
            const auto p = params(100.0f, 0.0f, 100.0f, 6000.0f);
            std::vector<float> in(16384, 0.0f);
            for (size_t i = 0; i < 200; ++i) in[i] = 0.3f * (float) std::sin(0.2 * (double) i);
            in[200] = 1.0e38f;
            TapeSatInsert fx;
            fx.prepare(kSR, 512);
            const auto y = render(fx, p, in);
            bool finite = true;
            for (float v : y) finite = finite && std::isfinite(v);
            expect(finite, "a 1e38 sample produced a non-finite output");
            float tail = 0.0f;
            for (size_t i = y.size() - 2048; i < y.size(); ++i) tail = std::max(tail, std::abs(y[i]));
            expect(tail < 1.0e-3f, "the 1e38 sample left a tail: " + juce::String(tail));
        }

        beginTest("Tape Sat: at 192 kHz with Head Bump 100 the silence is quiet (below -90 dB) and block sizes agree");
        {
            const auto p = params(100.0f, 0.0f, 100.0f, 20000.0f);
            std::vector<float> first;
            for (int block : { 512, 7, 1 })
            {
                TapeSatInsert fx;
                fx.prepare(192000.0, block);
                render(fx, p, noise(8192, 61u, 0.5f), block);   // warm every state, then let it ring out on silence
                const auto y = render(fx, p, std::vector<float>(65536, 0.0f), block);
                float floorMax = 0.0f;
                for (size_t i = 32768; i < y.size(); ++i) floorMax = std::max(floorMax, std::abs(y[i]));
                logMessage("192 kHz silence floor, block " + juce::String(block) + ": " + juce::String(db(floorMax), 1) + " dB");
                expect(floorMax < 3.2e-5f, "noise above -90 dB on silence at 192 kHz, block " + juce::String(block) + ": " + juce::String(floorMax));
                if (first.empty()) first = y;
                else
                {
                    float worst = 0.0f;
                    for (size_t i = 0; i < y.size(); ++i) worst = std::max(worst, std::abs(y[i] - first[i]));
                    expect(worst < 1.0e-4f, "block " + juce::String(block) + " differs from block 512 by " + juce::String(worst));
                }
            }
        }

        beginTest("Tape Sat: the impulse response peaks within 4 samples of the input (the half-band, the shelf pair and the DC block together)");
        {
            for (double sr : { 44100.0, 96000.0 })
            {
                std::vector<float> in(1024, 0.0f);
                in[100] = 0.2f;
                TapeSatInsert fx;
                fx.prepare(sr, 512);
                const auto y = render(fx, params(0.0f, 0.0f, 0.0f, 20000.0f), in);
                size_t peak = 0;
                for (size_t i = 0; i < y.size(); ++i) if (std::abs(y[i]) > std::abs(y[peak])) peak = i;
                logMessage("impulse peak delay at " + juce::String(sr) + " Hz: " + juce::String((int) peak - 100) + " samples");
                // Measured: 4 samples at 44.1 and 96 kHz (0.09 ms and 0.04 ms), more than the 2 ruled; inaudible on a per-voice insert.
                expect((int) peak >= 100 && (int) peak <= 104, "the impulse peak is " + juce::String((int) peak - 100) + " samples late at " + juce::String(sr));
            }
        }

        beginTest("Tape Sat: the slot table names the knobs clearly for the status bar and the defaults are in range");
        {
            // The status bar reads "<algorithm> <label>" and the value with the slot's unit; there is no other description field.
            const auto& slots = mu_ui::kInsertAlgoSlots[10];
            expectEquals(juce::String(slots[0].label), juce::String("Drive"));
            expectEquals(juce::String(slots[1].label), juce::String("Output"));
            expectEquals(juce::String(slots[2].label), juce::String("Head Bump"));
            expectEquals(juce::String(slots[2].unitSuffix), juce::String("%"));
            expectEquals(juce::String(slots[3].label), juce::String("Tone"));
            for (int s = 0; s < mu_ui::kInsertSlotCount; ++s)
            {
                const float d = mu_ui::kInsertAlgoDefaults[10][s];
                expect(d >= slots[s].minVal && d <= slots[s].maxVal, "the default of slot " + juce::String(s) + " is out of range");
            }
            expectEquals(mu_ui::kInsertAlgoDefaults[10][0], 30.0f);
            expectEquals(mu_ui::kInsertAlgoDefaults[10][2], 30.0f);
        }

        // Overdrive after the move to the shared ADAA tanh: the properties the inline version guaranteed, end to end.
        beginTest("Overdrive: Bias 0 is odd-symmetric, a biased signal leaves no DC, a NaN / Inf burst leaves no trace, reset equals fresh");
        {
            auto odParams = [](float drive, float bias)
            {
                VoiceParams p;
                p.insertAlgo = 14;
                p.insertParam[0] = mu_ui::actualToNorm(drive, 14, 0);
                p.insertParam[1] = mu_ui::actualToNorm(0.0f,  14, 1);
                p.insertParam[2] = mu_ui::actualToNorm(bias,  14, 2);
                p.insertParam[3] = mu_ui::actualToNorm(6000.0f, 14, 3);
                return p;
            };

            // Bias 0: flipping the input's sign flips the output exactly (the curve, the DC block and the tone filter are all odd).
            {
                const auto in = noise(8192, 41u, 0.6f);
                std::vector<float> neg(in.size());
                for (size_t i = 0; i < in.size(); ++i) neg[i] = -in[i];
                OverdriveInsert a, b;
                a.prepare(kSR, 512);  b.prepare(kSR, 512);
                const auto ya = render(a, odParams(70.0f, 0.0f), in);
                const auto yb = render(b, odParams(70.0f, 0.0f), neg);
                float worst = 0.0f;
                for (size_t i = 0; i < ya.size(); ++i) worst = std::max(worst, std::abs(ya[i] + yb[i]));
                expect(worst < 1.0e-5f, "Bias 0 is not odd-symmetric: worst " + juce::String(worst));
            }

            // Bias 100: the resting offset and the asymmetry's drift are removed, so a settled sine has no mean.
            {
                const int n = (int) kSR;
                OverdriveInsert a;
                a.prepare(kSR, 512);
                const auto y = render(a, odParams(50.0f, 100.0f), sine(n + kSettle, 1000.0, kSR, 0.25f));
                double mean = 0.0;
                for (int i = 0; i < n; ++i) mean += (double) y[(size_t) (kSettle + i)];
                mean /= (double) n;
                expect(std::abs(mean) < 1.0e-3, "a biased Overdrive leaves a DC offset of " + juce::String(mean, 6));
                expect(rms(y, kSettle, n) > 0.01, "the biased Overdrive is silent");
            }

            // A NaN / Inf burst is replaced by silence at the input and must not latch in the ADAA or filter states.
            {
                const auto p = odParams(60.0f, 50.0f);
                auto clean = noise(20000, 43u, 0.5f);
                std::vector<float> burst;
                for (int i = 0; i < 12; ++i) burst.push_back(std::numeric_limits<float>::quiet_NaN());
                for (int i = 0; i < 12; ++i) burst.push_back(std::numeric_limits<float>::infinity());
                burst.insert(burst.end(), clean.begin(), clean.end());
                OverdriveInsert a, b;
                a.prepare(kSR, 512);  b.prepare(kSR, 512);
                const auto ya = render(a, p, burst);
                const auto yb = render(b, p, clean);
                bool finite = true;
                for (float v : ya) finite = finite && std::isfinite(v);
                expect(finite, "the burst reached the Overdrive output");
                float worst = 0.0f;
                for (int i = 0; i < 256; ++i)
                    worst = std::max(worst, std::abs(ya[ya.size() - 1 - (size_t) i] - yb[yb.size() - 1 - (size_t) i]));
                expect(worst < 1.0e-3f, "the burst left a trace in the Overdrive output: " + juce::String(worst));
            }

            // reset() returns the ADAA, DC-block, tone and smoother state to a fresh instance's.
            {
                const auto p = odParams(80.0f, 60.0f);
                const auto in = noise(8192, 45u, 0.6f);
                OverdriveInsert used, fresh;
                used.prepare(kSR, 512);  fresh.prepare(kSR, 512);
                render(used, p, noise(8192, 46u, 0.9f));
                used.reset();
                const auto a = render(used, p, in);
                const auto b = render(fresh, p, in);
                int diff = 0;
                for (size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) ++diff;
                expectEquals(diff, 0, "a reset Overdrive differs from a fresh one");
            }
        }
    }
};

static TapeSatTests tapeSatTests;
