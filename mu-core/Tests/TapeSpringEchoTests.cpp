// The tape-echo-and-spring engine (Audio/FX/TapeSpringEcho.h): the mode table, repeats at one, two and three head spacings, the
// spring alone in mode 12, Intensity and Echo behaviour, bounded and finite at every extreme, and a rate change glides.

#include "Audio/FX/TapeSpringEcho.h"

#include <juce_core/juce_core.h>

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <vector>

namespace
{
constexpr double kSr = 48000.0;

struct Out { std::vector<float> l, r; };

Out render(const mu_core::TapeSpringEcho::Params& p, double seconds, bool noise = false, float impulse = 1.0f)
{
    mu_core::TapeSpringEcho e;
    e.setParameters(p);   // before prepare(): the tape starts at this speed instead of gliding from the default
    e.prepare(kSr);
    juce::Random rng(7);
    Out o;
    const int n = (int) (seconds * kSr);
    for (int i = 0; i < n; ++i)
    {
        const float in = noise ? rng.nextFloat() * 2.0f - 1.0f : (i == 0 ? impulse : 0.0f);
        float l = 0, r = 0;
        e.process(in, l, r);
        o.l.push_back(l);
        o.r.push_back(r);
    }
    return o;
}

// The sample index of the largest magnitude within [from, to).
int peakIn(const std::vector<float>& v, int from, int to)
{
    int best = from;
    for (int i = from; i < std::min(to, (int) v.size()); ++i)
        if (std::abs(v[(size_t) i]) > std::abs(v[(size_t) best])) best = i;
    return best;
}

double energy(const std::vector<float>& v, int from, int to)
{
    double s = 0.0;
    for (int i = from; i < std::min(to, (int) v.size()); ++i) s += (double) v[(size_t) i] * v[(size_t) i];
    return s;
}
}

struct TapeSpringEchoTests : public juce::UnitTest
{
    TapeSpringEchoTests() : juce::UnitTest("Tape echo and spring", "mu-core") {}

    void runTest() override
    {
        using E = mu_core::TapeSpringEcho;

        beginTest("the mode table: heads singly, then 2+3, then with the spring, then the spring alone");
        {
            expectEquals(E::modeHeads(1), 1); expectEquals(E::modeHeads(2), 2); expectEquals(E::modeHeads(3), 4);
            expectEquals(E::modeHeads(4), 6);
            expectEquals(E::modeHeads(5), 1); expectEquals(E::modeHeads(7), 4);
            expectEquals(E::modeHeads(11), 7); expectEquals(E::modeHeads(12), 0);
            for (int m = 1; m <= 4; ++m) expect(! E::modeReverb(m));
            for (int m = 5; m <= 12; ++m) expect(E::modeReverb(m));
            expectEquals(E::modeHeads(-3), 1); expectEquals(E::modeHeads(99), 0);   // clamped
        }

        beginTest("a single head repeats the input once, one, two or three spacings later");
        {
            E::Params p; p.rate = 0.5f; p.intensity = 0.0f; p.echo = 1.0f; p.reverb = 0.0f;
            const int spacing = (int) std::lround(E::headSpacingSeconds(p.rate) * kSr);
            for (int mode = 1; mode <= 3; ++mode)
            {
                p.mode = mode;
                const auto o = render(p, 1.0);
                const int expected = spacing * mode;
                const int found = peakIn(o.l, 10, (int) kSr);
                expectLessThan(std::abs(found - expected), 40, "mode " + juce::String(mode));   // wow and flutter move it a little
                expectGreaterThan(std::abs(o.l[(size_t) found]), 0.2f);
            }
        }

        beginTest("modes with two heads give two repeats, and the spring alone adds no echo");
        {
            E::Params p; p.rate = 0.7f; p.intensity = 0.0f; p.echo = 1.0f; p.reverb = 0.0f; p.mode = 4;   // heads 2 and 3
            const int spacing = (int) std::lround(E::headSpacingSeconds(p.rate) * kSr);
            const auto o = render(p, 1.0);
            expectGreaterThan(std::abs(o.l[(size_t) peakIn(o.l, spacing * 2 - 40, spacing * 2 + 40)]), 0.1f);
            expectGreaterThan(std::abs(o.l[(size_t) peakIn(o.l, spacing * 3 - 40, spacing * 3 + 40)]), 0.1f);
            expectLessThan(energy(o.l, 10, spacing * 2 - 100), 1.0e-9);          // nothing before head 2

            // Mode 12 is the spring only: the echo settings and the rate do not change it.
            E::Params a; a.mode = 12; a.rate = 0.1f; a.echo = 1.0f; a.intensity = 0.9f; a.reverb = 0.8f;
            E::Params b = a; b.rate = 0.9f; b.echo = 0.0f; b.intensity = 0.0f;
            const auto oa = render(a, 1.0, true), ob = render(b, 1.0, true);
            bool same = true;
            for (size_t i = 0; i < oa.l.size(); ++i) same = same && oa.l[i] == ob.l[i] && oa.r[i] == ob.r[i];
            expect(same);
            expectGreaterThan(energy(oa.l, 0, (int) kSr), 0.1);
        }

        beginTest("Intensity feeds the repeats back: more of them, each quieter; Echo 0 is silent in an echo-only mode");
        {
            E::Params p; p.mode = 3; p.rate = 1.0f; p.echo = 1.0f; p.reverb = 0.0f;
            const int spacing = (int) std::lround(E::headSpacingSeconds(1.0f) * kSr);
            p.intensity = 0.0f;
            const auto none = render(p, 2.0);
            p.intensity = 0.6f;
            const auto some = render(p, 2.0);
            const int third = spacing * 3;
            expectLessThan(energy(none.l, third * 2 - 400, third * 2 + 400), 1.0e-9);                // no second repeat
            expectGreaterThan(energy(some.l, third * 2 - 400, third * 2 + 400), 1.0e-5);            // a second repeat
            expectLessThan(energy(some.l, third * 3 - 400, third * 3 + 400), energy(some.l, third * 2 - 400, third * 2 + 400));
            p.echo = 0.0f;
            const auto quiet = render(p, 2.0);
            expectLessThan(energy(quiet.l, 0, (int) (2 * kSr)), 1.0e-12);
        }

        beginTest("finite and bounded at every extreme with full-scale noise, and with non-finite parameters");
        {
            for (int mode : { 1, 4, 7, 11, 12 })
                for (float rate : { 0.0f, 1.0f })
                    for (float intensity : { 0.0f, 1.0f })
                    {
                        E::Params p;
                        p.mode = mode; p.rate = rate; p.intensity = intensity; p.echo = 1.0f; p.reverb = 1.0f;
                        p.bass = 1.0f; p.treble = -1.0f; p.drive = 1.0f; p.hiss = 1.0f;
                        const auto o = render(p, 3.0, true);
                        bool ok = true; float peak = 0.0f;
                        for (size_t i = 0; i < o.l.size(); ++i)
                        {
                            ok = ok && std::isfinite(o.l[i]) && std::isfinite(o.r[i]);
                            peak = std::max({ peak, std::abs(o.l[i]), std::abs(o.r[i]) });
                        }
                        expect(ok);
                        expectLessThan(peak, 40.0f);
                    }
            E::Params bad;
            const float nan = std::numeric_limits<float>::quiet_NaN();
            bad.rate = nan; bad.intensity = nan; bad.echo = std::numeric_limits<float>::infinity(); bad.bass = -nan; bad.mode = -4;
            const auto o = render(bad, 1.0, true);
            bool ok = true;
            for (size_t i = 0; i < o.l.size(); ++i) ok = ok && std::isfinite(o.l[i]);
            expect(ok);
        }

        beginTest("Bass and Treble change the balance of the repeats; hiss adds a floor");
        {
            // A 60 Hz tone through the echo: a bass boost raises it, a cut lowers it; a 9 kHz tone answers to Treble.
            auto toneEnergy = [](float bass, float treble, float hz)
            {
                E::Params p; p.mode = 1; p.rate = 0.6f; p.echo = 1.0f; p.intensity = 0.0f; p.bass = bass; p.treble = treble;
                mu_core::TapeSpringEcho e;
                e.setParameters(p);
                e.prepare(kSr);
                double sum = 0.0;
                for (int i = 0; i < 48000; ++i)
                {
                    float l, r;
                    e.process(0.5f * std::sin(6.2831853f * hz * (float) i / (float) kSr), l, r);
                    if (i > 24000) sum += (double) l * l;
                }
                return sum;
            };
            expectGreaterThan(toneEnergy(1.0f, 0.0f, 60.0f), 3.0 * toneEnergy(0.0f, 0.0f, 60.0f));
            expectLessThan(toneEnergy(-1.0f, 0.0f, 60.0f), 0.4 * toneEnergy(0.0f, 0.0f, 60.0f));
            expectGreaterThan(toneEnergy(0.0f, 1.0f, 9000.0f), 3.0 * toneEnergy(0.0f, 0.0f, 9000.0f));
            E::Params q; q.mode = 1; q.echo = 1.0f; q.intensity = 0.0f;
            const auto clean = render(q, 1.0), hissy = [&] { auto h = q; h.hiss = 1.0f; return render(h, 1.0); }();
            expectGreaterThan(energy(hissy.l, 30000, 47000), energy(clean.l, 30000, 47000) + 1.0e-6);
        }

        beginTest("a Repeat Rate change glides (no jump in the output)");
        {
            mu_core::TapeSpringEcho e;
            e.prepare(kSr);
            E::Params p; p.mode = 1; p.rate = 0.2f; p.echo = 1.0f; p.intensity = 0.7f;
            e.setParameters(p);
            juce::Random rng(2);
            float prev = 0.0f, maxStep = 0.0f;
            for (int i = 0; i < 96000; ++i)
            {
                if (i == 48000) { p.rate = 0.9f; e.setParameters(p); }
                float l, r;
                e.process(0.3f * std::sin(0.03f * (float) i), l, r);
                if (i > 48000) maxStep = std::max(maxStep, std::abs(l - prev));
                prev = l;
            }
            expectLessThan(maxStep, 0.3f);
        }

        // Not a check: with MU_ECHO_DEMO_OUT (a folder) set, renders a plucked-note pattern through several settings to WAV files to listen to.
        const auto outDir = juce::SystemStats::getEnvironmentVariable("MU_ECHO_DEMO_OUT", {});
        if (outDir.isNotEmpty())
        {
            beginTest("demo render (not a check)");
            const juce::File dir(outDir);
            dir.createDirectory();
            struct Setting { const char* name; E::Params p; };
            auto make = [](int mode, float rate, float intensity, float echo, float reverb, float drive = 0.0f, float hiss = 0.0f)
            { E::Params p; p.mode = mode; p.rate = rate; p.intensity = intensity; p.echo = echo; p.reverb = reverb; p.drive = drive; p.hiss = hiss; return p; };
            const Setting settings[] = {
                { "mode1_head1", make(1, 0.6f, 0.35f, 0.8f, 0.0f) },   { "mode3_head3", make(3, 0.6f, 0.35f, 0.8f, 0.0f) },
                { "mode4_heads23", make(4, 0.6f, 0.4f, 0.8f, 0.0f) },  { "mode7_head3_spring", make(7, 0.6f, 0.4f, 0.7f, 0.5f) },
                { "mode11_all_spring", make(11, 0.55f, 0.45f, 0.7f, 0.5f) }, { "mode12_spring_only", make(12, 0.5f, 0.3f, 0.7f, 0.7f) },
                { "mode11_selfosc_drive", make(11, 0.5f, 0.95f, 0.7f, 0.4f, 0.5f, 0.2f) } };
            for (const auto& s : settings)
            {
                mu_core::TapeSpringEcho e;
                e.setParameters(s.p);
                e.prepare(kSr);
                const int total = (int) (kSr * 10.0);
                juce::AudioBuffer<float> buf(2, total);
                for (int i = 0; i < total; ++i)
                {
                    const double t = (double) i / kSr, inBeat = std::fmod(t, 1.0);
                    // A pluck every second, a lower one on the half beat of every other second.
                    float in = inBeat < 0.25 ? (float) (std::sin(2.0 * 3.14159265 * 330.0 * inBeat) * std::exp(-inBeat * 14.0)) : 0.0f;
                    if (std::fmod(t, 2.0) >= 1.0 && inBeat > 0.5 && inBeat < 0.75) in += (float) (std::sin(2.0 * 3.14159265 * 196.0 * (inBeat - 0.5)) * std::exp(-(inBeat - 0.5) * 12.0));
                    float l, r;
                    e.process(in * 0.7f, l, r);
                    buf.setSample(0, i, in * 0.7f + l);
                    buf.setSample(1, i, in * 0.7f + r);
                }
                const auto file = dir.getChildFile(juce::String("echo_") + s.name + ".wav");
                file.deleteFile();
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
                if (auto w = wav.createWriterFor(stream, juce::AudioFormatWriterOptions {}.withSampleRate(kSr).withNumChannels(2).withBitsPerSample(24)))
                    w->writeFromAudioSampleBuffer(buf, 0, total);
                logMessage("demo " + juce::String(s.name) + " -> " + file.getFullPathName());
            }
        }
    }
};

static TapeSpringEchoTests tapeSpringEchoTests;
