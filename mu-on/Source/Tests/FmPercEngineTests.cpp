// FM Perc voice tests: silence, tails, finite output at every extreme for all eight algorithm indices, repeatable
// noise, the algorithm latched at the trigger, velocity, tuning, no inert knob, and the listening aids (WAVs written
// when MU_FMPERC_AUDIO_DIR names a folder) plus an honest CPU-per-voice timing.

#include <juce_audio_formats/juce_audio_formats.h>
#include "Audio/FmPercEngine.h"

#include <cmath>
#include <vector>

using mu_on::FmPercEngine;

namespace
{
    constexpr double kSr = 48000.0;
    constexpr int    kBlock = 256;

    // The default Tom as the APVTS defaults set it (TrackParams.cpp).
    struct Knobs
    {
        int algo = 0;
        float tune = 90.0f, ratio = 1.5f, index = 0.35f, idec = 0.57f, fdbk = 0.05f;
        float dive = 0.43f, dtime = 0.46f, decay = 0.40f, fold = 0.15f, noise = 0.15f;

        void apply(FmPercEngine& e) const
        {
            e.setParams(algo, tune, ratio, index, idec, fdbk, dive, dtime, decay, fold, noise);
        }
    };

    // Renders `frames` samples of one engine (channel 0), firing `vel` at sample 0 when vel > 0.
    std::vector<float> render(FmPercEngine& e, const Knobs& k, int frames, float vel = 1.0f, int onset = 0)
    {
        std::vector<float> out((size_t) frames);
        juce::AudioBuffer<float> buf(1, kBlock);
        k.apply(e);
        if (vel > 0.0f) e.trigger(vel, onset);
        for (int pos = 0; pos < frames; pos += kBlock)
        {
            const int n = juce::jmin(kBlock, frames - pos);
            buf.clear();
            e.render(buf, n);
            for (int i = 0; i < n; ++i) out[(size_t) (pos + i)] = buf.getSample(0, i);
        }
        return out;
    }

    std::vector<float> renderFresh(const Knobs& k, int frames, float vel = 1.0f)
    {
        FmPercEngine e;
        e.prepare(kSr, kBlock);
        return render(e, k, frames, vel);
    }

    double rms(const std::vector<float>& v, size_t from, size_t to)
    {
        to = std::min(to, v.size());
        double s = 0.0;
        for (size_t i = from; i < to; ++i) s += (double) v[i] * v[i];
        return std::sqrt(s / (double) std::max<size_t>(1, to - from));
    }

    float peak(const std::vector<float>& v, size_t from = 0, size_t to = (size_t) -1)
    {
        to = std::min(to, v.size());
        float p = 0.0f;
        for (size_t i = from; i < to; ++i) p = std::max(p, std::abs(v[i]));
        return p;
    }

    // Upward zero crossings per second over [from, to): the fundamental of a clean tone.
    double crossingHz(const std::vector<float>& v, size_t from, size_t to)
    {
        int n = 0;
        for (size_t i = from + 1; i < to; ++i)
            if (v[i - 1] <= 0.0f && v[i] > 0.0f) ++n;
        return (double) n * kSr / (double) (to - from);
    }

    bool allFinite(const std::vector<float>& v)
    {
        for (float x : v) if (! std::isfinite(x)) return false;
        return true;
    }

    // A rough brightness measure: energy of the first difference over the energy of the signal.
    double brightness(const std::vector<float>& v, size_t n)
    {
        double d = 0.0, s = 1.0e-12;
        for (size_t i = 1; i < std::min(n, v.size()); ++i) { const double x = v[i] - v[i - 1]; d += x * x; s += (double) v[i] * v[i]; }
        return d / s;
    }

    void writeWav(const juce::File& file, const std::vector<float>& mono)
    {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
        if (stream == nullptr) return;
        auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}.withSampleRate(kSr).withNumChannels(1).withBitsPerSample(16));
        if (writer == nullptr) return;
        juce::AudioBuffer<float> b(1, (int) mono.size());
        b.copyFrom(0, 0, mono.data(), (int) mono.size());
        writer->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
    }

    // A sequence of hits in one buffer: each (start second, knobs, velocity), ringing tails summed.
    struct Hit { double at; Knobs k; float vel; };
    std::vector<float> renderSequence(const std::vector<Hit>& hits, double seconds)
    {
        FmPercEngine e;
        e.prepare(kSr, kBlock);
        const int total = (int) (seconds * kSr);
        std::vector<float> out((size_t) total, 0.0f);
        juce::AudioBuffer<float> buf(1, kBlock);
        size_t next = 0;
        for (int pos = 0; pos < total; pos += kBlock)
        {
            const int n = juce::jmin(kBlock, total - pos);
            // The hit that starts inside this block (one at a time is enough for the demos).
            if (next < hits.size())
            {
                const int at = (int) (hits[next].at * kSr);
                if (at >= pos && at < pos + n) { hits[next].k.apply(e); e.trigger(hits[next].vel, at - pos); ++next; }
            }
            buf.clear();
            e.render(buf, n);
            for (int i = 0; i < n; ++i) out[(size_t) (pos + i)] = buf.getSample(0, i);
        }
        return out;
    }
}

class FmPercEngineTest : public juce::UnitTest
{
public:
    FmPercEngineTest() : juce::UnitTest("mu-on FM Perc voice", "Engine") {}

    void runTest() override
    {
        beginTest("silent before a trigger, sounds on it, and the default Tom decays below -90 dB");
        {
            FmPercEngine e;
            e.prepare(kSr, kBlock);
            Knobs k;
            const auto idle = render(e, k, 4800, 0.0f);
            expect(peak(idle) == 0.0f, "no sound before a trigger");
            const auto hit = renderFresh(k, (int) (4.0 * kSr));
            expect(allFinite(hit), "finite");
            expect(peak(hit, 0, 4800) > 0.2f, "the hit is audible in the first 100 ms");
            expect(peak(hit, hit.size() - 4800) < 3.16e-5f, "below -90 dB after 4 s");
            expect(peak(hit) < 1.5f, "and bounded");
        }

        beginTest("the longest Decay at the lowest Tune is below -90 dB once it has died away");
        for (int algo = 0; algo < 8; ++algo)
        {
            Knobs k; k.algo = algo; k.tune = 40.0f; k.decay = 1.0f; k.noise = 1.0f;
            const auto hit = renderFresh(k, (int) (16.0 * kSr));
            expect(allFinite(hit), "finite");
            expect(peak(hit, hit.size() - 4800) < 3.16e-5f, "algorithm " + juce::String(algo) + " is silent below -90 dB after 16 s");
        }

        beginTest("no NaN or infinity and a bounded level at every parameter extreme, all eight algorithm indices");
        {
            const float tunes[]  = { 20.0f, 40.0f, 4000.0f, 8000.0f };
            const float ratios[] = { 0.25f, 0.5f, 6.0f, 8.0f };
            long checked = 0; bool finite = true; float worst = 0.0f;
            FmPercEngine e;
            e.prepare(kSr, kBlock);
            for (int algo = -1; algo <= 8; ++algo)
                for (float tune : tunes)
                    for (float ratio : ratios)
                        for (int mask = 0; mask < 256; ++mask)
                        {
                            if ((mask % 5) != 0 && algo != 0 && algo != 1) continue;   // thin the grid for the unbuilt algorithms
                            auto bit = [mask](int b) { return (mask >> b) & 1 ? 1.0f : 0.0f; };
                            Knobs k; k.algo = algo; k.tune = tune; k.ratio = ratio;
                            k.index = bit(0); k.idec = bit(1); k.fdbk = bit(2); k.dive = bit(3);
                            k.dtime = bit(4); k.decay = bit(5); k.fold = bit(6); k.noise = bit(7);
                            e.reset();
                            const auto out = render(e, k, 2400, 1.0f);
                            finite = finite && allFinite(out);
                            worst = std::max(worst, peak(out));
                            ++checked;
                        }
            logMessage("extremes checked: " + juce::String((int) checked) + ", worst peak " + juce::String(worst, 3));
            expect(finite, "every extreme renders finite samples");
            expect(worst < 2.5f, "and stays bounded (Noise at 1.0 adds up to about 1 on top of the body)");
        }

        beginTest("a render is deterministic: same seed, same output, and again after reset()");
        {
            Knobs k; k.noise = 0.8f; k.fdbk = 0.4f;
            const auto a = renderFresh(k, 24000);
            const auto b = renderFresh(k, 24000);
            expect(a == b, "two engines render identical samples");
            FmPercEngine e;
            e.prepare(kSr, kBlock);
            const auto c = render(e, k, 24000);
            e.reset();
            const auto d = render(e, k, 24000);
            expect(c == d, "and a reset engine renders the same again");
            FmPercEngine e2; e2.prepare(kSr, kBlock); e2.setNoiseSeed(3); e2.reset();
            expect(render(e2, k, 24000) != a, "a different slot seed gives different noise");
        }

        beginTest("the algorithm is latched at the trigger: changing it mid-tail changes nothing until the next hit");
        {
            Knobs tom; Knobs conga = tom; conga.algo = 1;
            FmPercEngine ref, mid;
            ref.prepare(kSr, kBlock); mid.prepare(kSr, kBlock);
            const auto base = render(ref, tom, 48000);
            // The same hit, but the algorithm choice flips to Conga after 100 ms.
            std::vector<float> flipped = render(mid, tom, 4800);
            auto rest = render(mid, conga, 48000 - 4800, 0.0f);
            flipped.insert(flipped.end(), rest.begin(), rest.end());
            expect(flipped == base, "the ringing tail is sample-identical");
            const auto next = render(mid, conga, 24000, 1.0f);
            expect(! next.empty() && peak(next) > 0.1f, "the next hit sounds");
            expect(rms(next, 0, 4800) != rms(renderFresh(tom, 24000), 0, 4800), "and uses the new algorithm");
        }

        beginTest("velocity scales the level and brightens the hit");
        {
            Knobs k;
            const auto loud = renderFresh(k, 24000, 1.0f);
            const auto soft = renderFresh(k, 24000, 0.3f);
            expect(rms(loud, 0, 12000) > 1.8 * rms(soft, 0, 12000), "a hard hit is clearly louder");
            logMessage("attack brightness: hard " + juce::String(brightness(loud, 600), 5) + ", soft " + juce::String(brightness(soft, 600), 5));
            expect(brightness(loud, 600) > brightness(soft, 600), "and brighter in the attack");
        }

        beginTest("Tune tracks pitch: the fundamental follows Tune at 70, 130 and 400 Hz");
        for (float hz : { 70.0f, 130.0f, 400.0f })
        {
            Knobs k; k.tune = hz; k.ratio = 1.0f; k.index = 0.0f; k.fdbk = 0.0f; k.dive = 0.0f; k.noise = 0.0f; k.fold = 0.0f; k.decay = 0.7f;
            const auto out = renderFresh(k, 24000);
            const double got = crossingHz(out, 2400, 14400);
            expect(std::abs(got - hz) < 0.02 * hz + 1.0, "Tune " + juce::String(hz) + " Hz measured " + juce::String(got, 1) + " Hz");
        }

        beginTest("the default Tom settles to its Tune after the dive");
        {
            Knobs k; k.tune = 130.0f; k.index = 0.0f; k.noise = 0.0f; k.fold = 0.0f;
            const auto out = renderFresh(k, (int) (0.8 * kSr));
            const double early = crossingHz(out, 480, 2880), late = crossingHz(out, 14400, 28800);
            expect(early > late * 1.1, "the first 50 ms are higher than the settled pitch (the dive)");
            expect(late > 105.0 && late < 170.0, "settled near 130 Hz (with the skin partial): " + juce::String(late, 1));
        }

        beginTest("no inert knob: moving any one of the ten changes the Tom and the Conga");
        for (int algo = 0; algo < 2; ++algo)
            for (int knob = 0; knob < 10; ++knob)
            {
                Knobs a; a.algo = algo; a.tune = algo ? 300.0f : 90.0f;
                Knobs lo = a, hi = a;
                float* lows[]  = { &lo.tune, &lo.ratio, &lo.index, &lo.idec, &lo.fdbk, &lo.dive, &lo.dtime, &lo.decay, &lo.fold, &lo.noise };
                float* highs[] = { &hi.tune, &hi.ratio, &hi.index, &hi.idec, &hi.fdbk, &hi.dive, &hi.dtime, &hi.decay, &hi.fold, &hi.noise };
                if (knob == 0) { *lows[0] = a.tune * 0.7f; *highs[0] = a.tune * 1.4f; }
                else if (knob == 1) { *lows[1] = 1.2f; *highs[1] = 3.0f; }
                else { *lows[knob] = 0.2f; *highs[knob] = 0.8f; }
                const auto x = renderFresh(lo, 12000), y = renderFresh(hi, 12000);
                double diff = 0.0;
                for (size_t i = 0; i < x.size(); ++i) diff += std::abs((double) x[i] - y[i]);
                expect(diff / (double) x.size() > 1.0e-4, "algorithm " + juce::String(algo) + " knob " + juce::String(knob) + " has an effect");
            }

        beginTest("Conga is shorter and higher-voiced than Tom; a retrigger over a tail and a mid-block onset are clean");
        {
            Knobs tom, conga; conga.algo = 1; conga.tune = 400.0f;
            const auto t = renderFresh(tom, 48000), c = renderFresh(conga, 48000);
            expect(rms(c, 24000, 28000) < rms(t, 24000, 28000), "the Conga has died further by 0.5 s");

            FmPercEngine e; e.prepare(kSr, kBlock);
            const auto before = render(e, tom, 2400, 1.0f, 100);
            expect(peak(before, 0, 100) == 0.0f, "silent before the onset offset");
            expect(peak(before, 100, 2400) > 0.05f, "and sounding after it");
            const auto again = render(e, tom, 4800, 1.0f);   // retrigger while ringing
            expect(allFinite(again) && peak(again) < 1.5f, "a retrigger over a ringing tail stays bounded");
            float jump = 0.0f;
            for (size_t i = 1; i < again.size(); ++i) jump = std::max(jump, std::abs(again[i] - again[i - 1]));
            expect(jump < 0.5f, "with no large step: biggest sample step " + juce::String(jump, 3));
        }
    }
};

static FmPercEngineTest fmPercEngineTest;

// The listening aids and the CPU numbers. WAVs are written only when MU_FMPERC_AUDIO_DIR is set.
class FmPercAidsTest : public juce::UnitTest
{
public:
    FmPercAidsTest() : juce::UnitTest("mu-on FM Perc listening aids and CPU", "Engine") {}

    void runTest() override
    {
        const auto dirName = juce::SystemStats::getEnvironmentVariable("MU_FMPERC_AUDIO_DIR", {});
        if (dirName.isNotEmpty())
        {
            beginTest("write the listening WAVs");
            const juce::File dir(dirName);
            dir.createDirectory();

            Knobs tom;
            writeWav(dir.getChildFile("01-default-tom-90hz.wav"), renderSequence({ {0.1, tom, 1.0f}, {1.3, tom, 0.5f}, {2.5, tom, 0.8f} }, 3.8));

            auto at = [&](float hz, int algo = 0) { Knobs k; k.tune = hz; k.algo = algo; return k; };
            writeWav(dir.getChildFile("02-tom-set-low-mid-high-70-110-170.wav"),
                     renderSequence({ {0.1, at(70), 1.0f}, {1.0, at(110), 1.0f}, {1.9, at(170), 1.0f}, {2.8, at(70), 0.9f},
                                      {3.3, at(110), 0.9f}, {3.8, at(170), 0.9f}, {4.2, at(110), 0.7f}, {4.6, at(70), 1.0f} }, 6.2));

            // Ratio colours of the Tom: plain sine, round tonk, natural head, tight, ringy.
            std::vector<Hit> ratios;
            double t = 0.1;
            for (float r : { 1.0f, 1.41f, 1.59f, 2.2f, 3.5f }) { Knobs k = at(110); k.ratio = r; ratios.push_back({ t, k, 1.0f }); t += 1.0; }
            writeWav(dir.getChildFile("03-tom-ratios-1.0-1.41-1.59-2.2-3.5.wav"), renderSequence(ratios, 5.6));

            Knobs conga = at(400, 1); conga.ratio = 2.3f;
            Knobs congaLow = at(220, 1); congaLow.ratio = 2.3f;
            writeWav(dir.getChildFile("04-conga-high-and-low.wav"),
                     renderSequence({ {0.1, conga, 1.0f}, {0.6, conga, 0.6f}, {1.0, congaLow, 1.0f}, {1.5, conga, 0.8f},
                                      {1.8, conga, 0.5f}, {2.2, congaLow, 0.9f} }, 3.4));

            Knobs electro = at(120); electro.ratio = 1.0f; electro.dive = 1.0f; electro.dtime = 0.55f; electro.noise = 0.5f; electro.index = 0.2f;
            writeWav(dir.getChildFile("05-tom-electro-big-dive.wav"), renderSequence({ {0.1, electro, 1.0f}, {1.0, electro, 0.6f}, {1.8, electro, 1.0f} }, 3.0));
            logMessage("listening WAVs written to " + dir.getFullPathName());
        }

        beginTest("CPU per voice (honest timing; the config is stated in the log)");
        {
            // One voice retriggered every 125 ms for 20 s of audio: busy about as much as a fast fill.
            for (int algo = 0; algo < 2; ++algo)
            {
                Knobs k; k.algo = algo; k.tune = algo ? 400.0f : 90.0f;
                FmPercEngine e;
                e.prepare(kSr, kBlock);
                k.apply(e);
                juce::AudioBuffer<float> buf(2, kBlock);
                const int blocks = (int) (20.0 * kSr / kBlock);
                const int retrig = (int) (0.125 * kSr / kBlock);
                const auto t0 = juce::Time::getHighResolutionTicks();
                float sink = 0.0f;
                for (int b = 0; b < blocks; ++b)
                {
                    if (b % retrig == 0) e.trigger(1.0f, 0);
                    buf.clear();
                    e.render(buf, kBlock);
                    sink += buf.getSample(0, 7);
                }
                const double sec = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - t0);
                const double audio = (double) blocks * kBlock / kSr;
                const double ns = sec / ((double) blocks * kBlock) * 1.0e9;
                logMessage(juce::String(algo ? "Conga" : "Tom") + ": " + juce::String(ns, 1) + " ns per sample while sounding = "
                           + juce::String(100.0 * sec / audio, 3) + " % of one core at 48 kHz"
#if JUCE_DEBUG
                           + " (DEBUG build, unoptimised)"
#else
                           + " (optimised build)"
#endif
                           + (sink == 12345.678f ? "!" : ""));
                expect(sec < audio, "faster than real time");
            }
        }
    }
};

static FmPercAidsTest fmPercAidsTest;
