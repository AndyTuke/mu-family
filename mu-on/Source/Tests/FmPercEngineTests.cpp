// FM Perc voice tests: silence, tails, finite output at every extreme for all eight algorithm indices, repeatable
// noise, the algorithm latched at the trigger, velocity, tuning, no inert knob, and the listening aids (WAVs written
// when MU_FMPERC_AUDIO_DIR names a folder) plus an honest CPU-per-voice timing.

#include <juce_audio_formats/juce_audio_formats.h>
#include "Audio/FmPercEngine.h"
#include "Plugin/PluginProcessor.h"
#include "Sequencer/TrackTypes.h"
#include "Tests/ProcessorSeamHelpers.h"

#include <algorithm>
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
        const float top = b.getMagnitude(0, b.getNumSamples());
        if (top > 0.98f) b.applyGain(0.98f / top);   // 16-bit files must not clip; only a too-hot render is scaled down
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

        beginTest("every algorithm is silent before a trigger and decays below -90 dB at default settings and at the extreme tunes");
        for (int algo = 0; algo < 8; ++algo)
            for (int cfg = 0; cfg < 3; ++cfg)
            {
                Knobs k; k.algo = algo;
                if (cfg == 1) { k.tune = 20.0f;   k.idec = 1.0f; k.decay = 1.0f; k.noise = 1.0f; k.fdbk = 1.0f; k.index = 1.0f; }
                if (cfg == 2) { k.tune = 8000.0f; k.idec = 1.0f; k.decay = 1.0f; k.noise = 1.0f; k.fdbk = 1.0f; k.index = 1.0f; k.ratio = 8.0f; }
                const juce::String what = "algorithm " + juce::String(algo) + " config " + juce::String(cfg);
                FmPercEngine e; e.prepare(kSr, kBlock);
                const auto idle = render(e, k, 2400, 0.0f);
                expect(peak(idle) == 0.0f, what + " is silent before a trigger");
                const auto hit = render(e, k, (int) (16.0 * kSr));
                expect(allFinite(hit), what + " is finite");
                expect(peak(hit, 0, 4800) > 0.02f, what + " sounds");
                expect(peak(hit, hit.size() - 4800) < 3.16e-5f, what + " is below -90 dB after 16 s");
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
                            if ((mask % 3) != 0 && algo != 0 && algo != 1) continue;   // thin the grid for the newer voicings
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

        beginTest("the algorithm is latched for the oversampled and multi-operator voices too (Zap, Clank, Burst)");
        for (int a = 2; a < 8; a += 2)
        {
            Knobs from; from.algo = a; from.tune = 600.0f;
            Knobs to = from; to.algo = (a + 1) % 8;
            FmPercEngine ref, mid;
            ref.prepare(kSr, kBlock); mid.prepare(kSr, kBlock);
            const auto base = render(ref, from, 24000);
            std::vector<float> flipped = render(mid, from, 4800);
            auto rest = render(mid, to, 24000 - 4800, 0.0f);
            flipped.insert(flipped.end(), rest.begin(), rest.end());
            expect(flipped == base, "algorithm " + juce::String(a) + ": the ringing tail is sample-identical");
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

        beginTest("no inert knob: moving any one of the ten changes every one of the eight voices");
        for (int algo = 0; algo < 8; ++algo)
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

        beginTest("the eight voices are measurably different from each other (pitch, brightness, length)");
        {
            struct Feature { double zc, bright, t40ms; };
            std::vector<Feature> f;
            for (int algo = 0; algo < 8; ++algo)
            {
                Knobs k; k.algo = algo; k.tune = 300.0f;
                const auto out = renderFresh(k, (int) (3.0 * kSr));
                // Time for the 5 ms RMS to fall 40 dB below its loudest window.
                const size_t win = 240;
                double top = 0.0;
                std::vector<double> env;
                for (size_t i = 0; i + win <= out.size(); i += win) { env.push_back(rms(out, i, i + win)); top = std::max(top, env.back()); }
                size_t at = 0;
                while (at < env.size() && env[at] < top) ++at;
                while (at < env.size() && env[at] > top * 0.01) ++at;
                f.push_back({ crossingHz(out, 0, 4800), brightness(out, 2400), (double) at * 5.0 });
                logMessage("algorithm " + juce::String(algo) + ": zero-crossing " + juce::String(f.back().zc, 0) + " Hz, brightness "
                           + juce::String(f.back().bright, 4) + ", 40 dB fall at " + juce::String(f.back().t40ms, 0) + " ms");
            }
            int closePairs = 0;
            for (size_t a = 0; a < f.size(); ++a)
                for (size_t b = a + 1; b < f.size(); ++b)
                {
                    const bool differs = std::abs(std::log(f[a].zc / f[b].zc)) > 0.15
                                      || std::abs(std::log(f[a].bright / f[b].bright)) > 0.25
                                      || std::abs(std::log((f[a].t40ms + 5.0) / (f[b].t40ms + 5.0))) > 0.25;
                    if (! differs) { ++closePairs; logMessage("too alike: " + juce::String((int) a) + " and " + juce::String((int) b)); }
                }
            expectEquals(closePairs, 0, "no two voices share pitch content, brightness and length");
        }

        beginTest("the alias option changes only the oversampled voices (Zap, Clank, Burst) and is off by default");
        for (int algo = 0; algo < 8; ++algo)
        {
            Knobs k; k.algo = algo; k.tune = 1200.0f; k.index = 0.8f; k.dive = 0.9f;
            FmPercEngine clean, aliased;
            clean.prepare(kSr, kBlock); aliased.prepare(kSr, kBlock);
            aliased.setDeliberateAlias(true);
            const auto x = render(clean, k, 9600), y = render(aliased, k, 9600);
            const bool oversampled = algo == 2 || algo == 4 || algo == 7;
            expect(oversampled == (x != y), "algorithm " + juce::String(algo) + (oversampled ? " changes with the alias option" : " ignores the alias option"));
            expect(allFinite(y), "and the aliased render is finite");
        }
    }
};

static FmPercEngineTest fmPercEngineTest;


// The factory presets (mu-on/factory/Tracks, written by mu-on/tools/gen_fmperc_presets.py), by voice in design order.
namespace
{
    const std::vector<std::vector<juce::String>>& presetGroups()
    {
        static const std::vector<std::vector<juce::String>> g {
            { "Low Tom", "Mid Tom", "High Tom", "Floor Tom", "Electro Tom", "Rack Tom Tight" },
            { "Low Conga", "High Conga", "Bongo", "Timbale" },
            { "Laser Zap", "Screamer", "Blip", "Pew Low" },
            { "Rim Tick", "Clave", "Wood Block", "Grit Tick" },
            { "Closed Clank", "Open Clank", "Gong Hit", "Industrial" },
            { "Marimba Tap", "Mallet Low", "Mallet High" },
            { "Ping", "Rising Ping", "Acid Stab" },
            { "Glitch Burst", "Digital Crunch", "Noise Hit" } };
        return g;
    }
    const char* const kVoiceSlug[8] = { "tom", "conga", "zap", "click-rim", "clank", "wood-mallet", "pluck-ping", "burst-glitch" };

    juce::File presetFile(const juce::String& name) { return juce::File(MU_ON_FACTORY_TRACKS_DIR).getChildFile(name + ".muTrack"); }

    // The knobs a preset file's rows hold (plain values), or false when the file is missing or unreadable.
    bool knobsFromFile(const juce::File& file, Knobs& k)
    {
        const auto xml = juce::XmlDocument::parse(file);
        if (xml == nullptr) return false;
        const auto tree = juce::ValueTree::fromXml(*xml);
        int found = 0;
        for (int i = 0; i < tree.getNumChildren(); ++i)
        {
            const auto row = tree.getChild(i);
            if (! row.hasType("p")) continue;
            const juce::String id = row.getProperty("id").toString();
            const float x = (float) (double) row.getProperty("x", 0.0);
            if      (id == "fmp_algo")  { k.algo = juce::roundToInt(x); ++found; }
            else if (id == "fmp_tune")  { k.tune = x;  ++found; }
            else if (id == "fmp_ratio") { k.ratio = x; ++found; }
            else if (id == "fmp_index") { k.index = x; ++found; }
            else if (id == "fmp_idec")  { k.idec = x;  ++found; }
            else if (id == "fmp_fdbk")  { k.fdbk = x;  ++found; }
            else if (id == "fmp_dive")  { k.dive = x;  ++found; }
            else if (id == "fmp_dtime") { k.dtime = x; ++found; }
            else if (id == "fmp_decay") { k.decay = x; ++found; }
            else if (id == "fmp_fold")  { k.fold = x;  ++found; }
            else if (id == "fmp_noise") { k.noise = x; ++found; }
        }
        return found == 11;
    }
}

class FmPercPresetTest : public juce::UnitTest
{
public:
    FmPercPresetTest() : juce::UnitTest("mu-on FM Perc factory presets", "Engine") {}

    void runTest() override
    {
        using namespace mu_seam_test;
        using mu_on::PluginProcessor;

        beginTest("all 31 factory presets exist, name FM Perc, and carry their voice's algorithm");
        int total = 0;
        for (int algo = 0; algo < 8; ++algo)
            for (const auto& pname : presetGroups()[(size_t) algo])
            {
                ++total;
                const auto file = presetFile(pname);
                expect(file.existsAsFile(), pname + " exists");
                const auto xml = juce::XmlDocument::parse(file);
                expect(xml != nullptr && xml->hasTagName(mu_on::kTrackPresetTag), pname + " is a track preset");
                if (xml == nullptr) continue;
                const auto tree = juce::ValueTree::fromXml(*xml);
                // typedSlotPresetFiles keeps the files whose type equals the slot's type: only FM Perc's filter accepts these.
                expect(mu_on::typeFromPreset(tree) == mu_core::LayerTypeId::FmPerc, pname + " is typed FM Perc");
                expect(mu_on::kFmPercType.typeFromPreset(tree) == mu_on::kFmPercType.id, pname + " passes the FM Perc slot filter");
                expect(mu_on::kKickType.typeFromPreset(tree) != mu_on::kKickType.id && mu_on::kRumbleType.typeFromPreset(tree) != mu_on::kRumbleType.id,
                       pname + " is rejected by the other types' filters");
                Knobs k;
                expect(knobsFromFile(file, k) && k.algo == algo, pname + " holds all eleven rows and algorithm " + juce::String(algo));
            }
        expectEquals(total, 31);
        expectEquals(juce::File(MU_ON_FACTORY_TRACKS_DIR).findChildFiles(juce::File::findFiles, false, "*.muTrack").size(), 31, "and no stray files in the folder");
        expect(juce::String(mu_on::kFmPercType.presetFolder) == mu_on::kKickType.presetFolder
                   && juce::String(mu_on::kFmPercType.contentFolderName) == mu_on::kKickType.contentFolderName
                   && juce::String(mu_on::kFmPercType.presetExtension) == mu_on::kKickType.presetExtension,
               "FM Perc reads the same Tracks folder and extension as the other mu-On types");

        beginTest("every factory preset loads through the real processor, sounds, is finite and decays below -90 dB");
        {
            TempStorage storage;
            PluginProcessor p;
            for (int algo = 0; algo < 8; ++algo)
                for (const auto& pname : presetGroups()[(size_t) algo])
                {
                    const auto file = presetFile(pname);
                    p.loadLayerPreset(0, file);
                    expectEquals((int) getParam(p.apvts, "t0_eng"), 5, pname + ": the preset set the FM Perc engine");
                    expectEquals((int) getParam(p.apvts, "t0_fmp_algo"), algo, pname + ": the algorithm");

                    // The knobs as the track now holds them must be the file's, and they must render.
                    Knobs fromFile, fromTrack;
                    expect(knobsFromFile(file, fromFile), pname + " parses");
                    fromTrack.algo  = (int) getParam(p.apvts, "t0_fmp_algo");
                    fromTrack.tune  = getParam(p.apvts, "t0_fmp_tune");   fromTrack.ratio = getParam(p.apvts, "t0_fmp_ratio");
                    fromTrack.index = getParam(p.apvts, "t0_fmp_index");  fromTrack.idec  = getParam(p.apvts, "t0_fmp_idec");
                    fromTrack.fdbk  = getParam(p.apvts, "t0_fmp_fdbk");   fromTrack.dive  = getParam(p.apvts, "t0_fmp_dive");
                    fromTrack.dtime = getParam(p.apvts, "t0_fmp_dtime");  fromTrack.decay = getParam(p.apvts, "t0_fmp_decay");
                    fromTrack.fold  = getParam(p.apvts, "t0_fmp_fold");   fromTrack.noise = getParam(p.apvts, "t0_fmp_noise");
                    expect(std::abs(fromTrack.tune - fromFile.tune) < 0.2f && std::abs(fromTrack.ratio - fromFile.ratio) < 0.02f
                           && std::abs(fromTrack.index - fromFile.index) < 0.002f && std::abs(fromTrack.decay - fromFile.decay) < 0.002f
                           && std::abs(fromTrack.noise - fromFile.noise) < 0.002f, pname + ": the track holds the file's values");

                    const auto out = renderFresh(fromTrack, (int) (12.0 * kSr));
                    expect(allFinite(out), pname + " renders finite");
                    expect(peak(out, 0, 24000) > 0.05f, pname + " sounds (peak " + juce::String(peak(out, 0, 24000), 3) + ")");
                    expect(peak(out) < 2.0f, pname + " stays bounded");
                    expect(peak(out, out.size() - 4800) < 3.16e-5f, pname + " is below -90 dB after 12 s");
                }
        }
    }
};

static FmPercPresetTest fmPercPresetTest;

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

            // One WAV per voice at the APVTS default settings (Tune 90 Hz, Ratio 1.5, ...): three hits, hard, soft, hard.
            for (int algo = 0; algo < 8; ++algo)
            {
                Knobs d; d.algo = algo;
                writeWav(dir.getChildFile("1" + juce::String(algo) + "-voice-" + kVoiceSlug[algo] + "-default.wav"),
                         renderSequence({ {0.1, d, 1.0f}, {1.4, d, 0.5f}, {2.7, d, 1.0f} }, 4.3));
            }

            // One WAV per factory-preset group: the presets in order, 1.2 s apart, each at full velocity.
            for (int algo = 0; algo < 8; ++algo)
            {
                std::vector<Hit> hits;
                double at0 = 0.1;
                for (const auto& pname : presetGroups()[(size_t) algo])
                {
                    Knobs k;
                    if (knobsFromFile(presetFile(pname), k)) hits.push_back({ at0, k, 1.0f });
                    at0 += 1.2;
                }
                if (! hits.empty())
                    writeWav(dir.getChildFile("2" + juce::String(algo) + "-presets-" + kVoiceSlug[algo] + ".wav"), renderSequence(hits, at0 + 0.4));
            }
            logMessage("listening WAVs written to " + dir.getFullPathName());
        }

        beginTest("CPU per voice (honest timing; the config is stated in the log)");
        {
            // One voice retriggered every 125 ms for 20 s of audio: busy about as much as a fast fill.
            const char* names[8] = { "Tom", "Conga", "Zap", "Click", "Clank", "Wood", "Pluck", "Burst" };
            const float tunes[8] = { 90.0f, 400.0f, 800.0f, 2000.0f, 1200.0f, 440.0f, 520.0f, 1000.0f };
            for (int algo = 0; algo < 8; ++algo)
            {
                Knobs k; k.algo = algo; k.tune = tunes[algo];
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
                logMessage(juce::String(names[algo]) + ": " + juce::String(ns, 1) + " ns per sample while sounding = "
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
