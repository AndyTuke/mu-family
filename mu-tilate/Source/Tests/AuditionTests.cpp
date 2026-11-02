// Audition renders: the Phrase engine plays a synthetic loop with known tones and timing through the tempo modes and the
// timeline edits, each scenario is written to a WAV under tests/_out/tilate and measured (onset times, pitch, level, clicks),
// and the numbers are checked against what the design says the result must be. The metrics are printed per scenario and
// collected in report.json, so a session can judge how a change sounds without listening (docs/mu-tilate/test-plan.md).

#include "Sequencer/PhrasePlayback.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>

using namespace mu_tilate;

namespace
{
constexpr double kSr = 48000.0;
constexpr double kTones[4] = { 220.0, 330.0, 440.0, 550.0 };   // slice k's pitch in Hz

juce::File outDir()
{
    // tests/_out/tilate next to the source tree this file was compiled from.
    auto dir = juce::File(__FILE__).getParentDirectory().getParentDirectory().getParentDirectory().getParentDirectory()
                   .getChildFile("tests").getChildFile("_out").getChildFile("tilate");
    dir.createDirectory();
    return dir;
}

// The loop: four one-beat slices at 120 BPM (0.5 s each), each a decaying sine of its own pitch with a short noise click at its start.
struct Material
{
    juce::AudioBuffer<float> audio { 1, 96000 };
    SliceList slices { 96000 };
    Material()
    {
        juce::Random rng(11);
        for (int k = 0; k < 4; ++k)
            for (int i = 0; i < 24000; ++i)
            {
                const double t = (double) i / kSr;
                float v = 0.8f * (float) (std::exp(-t / 0.35) * std::sin(2.0 * juce::MathConstants<double>::pi * kTones[k] * t));
                if (i < 120) v += 0.3f * (rng.nextFloat() * 2.0f - 1.0f) * (1.0f - (float) i / 120.0f);   // the click that marks the attack
                audio.setSample(0, k * 24000 + i, v);
            }
        slices.divideEvenly(4);
    }
};

struct Scenario
{
    juce::String name;
    PhraseSettings settings;
    Timeline timeline { 4.0 };
    double bpm = 120.0;
    double seconds = 2.0;
};

juce::AudioBuffer<float> render(const Material& m, Scenario& sc)
{
    PhraseLoop loop;
    loop.audio = &m.audio; loop.fileSampleRate = kSr; loop.slices = &m.slices; loop.timeline = &sc.timeline;
    PhrasePlayback pb;
    pb.prepare(kSr, 512);
    const int total = (int) std::lround(sc.seconds * kSr);
    juce::AudioBuffer<float> out(1, total);
    out.clear();
    for (int pos = 0; pos < total; pos += 256)
    {
        const int n = std::min(256, total - pos);
        PhraseClock c;
        c.playing = true; c.bpm = sc.bpm; c.sampleRate = kSr;
        c.beatsPerSample = sc.bpm / 60.0 / kSr;
        c.startBeat = (double) pos * c.beatsPerSample;
        juce::AudioBuffer<float> view(out.getArrayOfWritePointers(), 1, pos, n);
        pb.render(loop, sc.settings, c, view, n);
    }
    return out;
}

// Times in seconds where the 2 ms envelope rises through a quarter of the peak after at least 20 ms below a twelfth of it.
std::vector<double> onsets(const juce::AudioBuffer<float>& b)
{
    const int n = b.getNumSamples(), win = (int) (0.002 * kSr);
    std::vector<float> env((size_t) n, 0.0f);
    float peak = 1.0e-6f, run = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        run += (std::abs(b.getSample(0, i)) - run) * (1.0f / (float) win);
        env[(size_t) i] = run;
        peak = std::max(peak, run);
    }
    std::vector<double> out;
    int quiet = 0;
    bool armed = true;   // ready for an attack: the signal has been low for a while (or it is the start)
    for (int i = 0; i < n; ++i)
    {
        if (env[(size_t) i] < peak / 12.0f)
        {
            if (++quiet >= (int) (0.02 * kSr)) armed = true;
        }
        else
        {
            quiet = 0;
            if (armed && env[(size_t) i] > peak / 4.0f)
            {
                // The envelope lags the attack by a few samples; walk back to where it left the floor.
                int j = i;
                while (j > 0 && env[(size_t) j - 1] > peak / 40.0f) --j;
                out.push_back((double) j / kSr);
                armed = false;
            }
        }
    }
    return out;
}

// Times in seconds of the noise click that starts each slice: the first sample after 100 ms of quiet where the sample-to-sample step
// passes a quarter of the largest step (the tones' own slope is far below that).
std::vector<double> clicks(const juce::AudioBuffer<float>& b)
{
    const int n = b.getNumSamples();
    float top = 1.0e-6f;
    for (int i = 1; i < n; ++i) top = std::max(top, std::abs(b.getSample(0, i) - b.getSample(0, i - 1)));
    std::vector<double> out;
    int last = -(1 << 20);
    for (int i = 1; i < n; ++i)
        if (std::abs(b.getSample(0, i) - b.getSample(0, i - 1)) > top * 0.25f)
        {
            if (i - last > (int) (0.1 * kSr)) out.push_back((double) i / kSr);
            last = i;
        }
    return out;
}

// The pitch of a tone in Hz from the autocorrelation over [from, from + len) (150..900 Hz), 0 when it is too quiet or unclear.
double pitchAt(const juce::AudioBuffer<float>& b, int from, int len = 4096)
{
    if (from < 0 || from + len + 400 > b.getNumSamples()) return 0.0;
    const int minLag = (int) (kSr / 900.0), maxLag = (int) (kSr / 150.0);
    double energy = 0.0;
    for (int i = 0; i < len; ++i) energy += (double) b.getSample(0, from + i) * b.getSample(0, from + i);
    if (energy / len < 1.0e-5) return 0.0;
    std::vector<double> ac((size_t) maxLag + 2, 0.0);
    for (int lag = minLag - 1; lag <= maxLag + 1; ++lag)
    {
        double s = 0.0;
        for (int i = 0; i < len; ++i) s += (double) b.getSample(0, from + i) * b.getSample(0, from + i + lag);
        ac[(size_t) lag] = s;
    }
    int best = minLag;
    for (int lag = minLag; lag <= maxLag; ++lag) if (ac[(size_t) lag] > ac[(size_t) best]) best = lag;
    const double a = ac[(size_t) best - 1], c = ac[(size_t) best], d = ac[(size_t) best + 1];
    const double shift = (a - d) / (2.0 * (a - 2.0 * c + d) + 1.0e-12);   // parabolic peak
    return kSr / ((double) best + shift);
}

double rmsOf(const juce::AudioBuffer<float>& b, double fromSec, double lenSec)
{
    const int from = std::max(0, (int) (fromSec * kSr)), n = std::min((int) (lenSec * kSr), b.getNumSamples() - from);
    double s = 0.0;
    for (int i = 0; i < n; ++i) s += (double) b.getSample(0, from + i) * b.getSample(0, from + i);
    return n > 0 ? std::sqrt(s / n) : 0.0;
}

double maxStep(const juce::AudioBuffer<float>& b, double fromSec, double toSec)
{
    double m = 0.0;
    for (int i = std::max(1, (int) (fromSec * kSr)); i < std::min((int) (toSec * kSr), b.getNumSamples()); ++i)
        m = std::max(m, (double) std::abs(b.getSample(0, i) - b.getSample(0, i - 1)));
    return m;
}

void writeWav(const juce::AudioBuffer<float>& b, const juce::String& name)
{
    auto f = outDir().getChildFile(name + ".wav");
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream(f.createOutputStream());
    if (stream == nullptr) return;
    auto w = wav.createWriterFor(stream, juce::AudioFormatWriterOptions {}.withSampleRate(kSr).withNumChannels(1).withBitsPerSample(16));
    if (w != nullptr) w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
}
}

struct AuditionTests : public juce::UnitTest
{
    AuditionTests() : juce::UnitTest("Phrase audition renders", "mu-tilate-audition") {}

    juce::String report;

    void note(const juce::String& scenarioName, const juce::String& metrics)
    {
        logMessage("  [" + scenarioName + "] " + metrics);
        report << "  { \"scenario\": \"" << scenarioName << "\", \"metrics\": \"" << metrics.replace("\"", "'") << "\" },\n";
    }

    // Every beat's onset where the tempo puts it, and (when `ratio` > 0) every slice's pitch at that ratio of its own.
    void checkBeats(const juce::AudioBuffer<float>& out, const juce::String& label, double beatSec, double ratio, double pitchTol)
    {
        const auto on = clicks(out);
        juce::String m = "onsets";
        for (double t : on) m << " " << juce::String(t, 3);
        expectEquals((int) on.size(), 4, label + ": four attacks");
        for (int k = 0; k < (int) on.size() && k < 4; ++k)
        {
            expectWithinAbsoluteError(on[(size_t) k], k * beatSec, 0.006, label + ": attack " + juce::String(k + 1) + " on its beat");
            const double f = pitchAt(out, (int) ((on[(size_t) k] + 0.03) * kSr));
            m << (k == 0 ? "; pitch" : "") << " " << juce::String(f, 1);
            if (ratio > 0.0) expectWithinAbsoluteError(f / (kTones[k] * ratio), 1.0, pitchTol, label + ": pitch of slice " + juce::String(k + 1));
        }
        note(label, m);
    }

    void runTest() override
    {
        Material m;
        report = "[\n";

        auto allBeats = [](Scenario& sc) { for (int k = 0; k < 4; ++k) sc.timeline.place(k, (double) k, 0.0); };

        beginTest("off: the loop plays at its own tempo and pitch");
        {
            Scenario sc; sc.name = "off_120"; sc.settings.mode = TempoMode::Off; allBeats(sc);
            const auto out = render(m, sc);
            writeWav(out, sc.name);
            checkBeats(out, sc.name, 0.5, 1.0, 0.01);
        }

        struct Case { const char* name; double bpm; float mix; };
        const Case cases[] = { { "match_150_repitch", 150.0, 0.0f }, { "match_150_stretch", 150.0, 1.0f },
                               { "match_150_blend", 150.0, 0.5f }, { "match_90_stretch", 90.0, 1.0f },
                               { "match_90_repitch", 90.0, 0.0f } };
        for (const auto& c : cases)
        {
            beginTest(juce::String("tempo match: ") + c.name);
            Scenario sc; sc.name = c.name; sc.bpm = c.bpm; sc.seconds = 4.0 * 60.0 / c.bpm - 0.02;   // one pass of the four-beat timeline (it would wrap and sound again after)
            sc.settings.mode = TempoMode::Match; sc.settings.stretchMix = c.mix; allBeats(sc);
            const auto out = render(m, sc);
            writeWav(out, sc.name);
            const double ratio = c.bpm / 120.0;
            // The pitch follows the tempo by the re-pitch share of the ratio: all of it at mix 0, none at mix 1.
            checkBeats(out, sc.name, 60.0 / c.bpm, std::pow(ratio, 1.0 - (double) c.mix), c.mix > 0.0f && c.mix < 1.0f ? 0.04 : 0.03);
        }

        beginTest("trim: a block started half a beat in plays the rest of the slice from there");
        {
            Scenario sc; sc.name = "trim_start"; sc.settings.mode = TempoMode::Off;
            sc.timeline.place(0, 0.0, 0.0);
            sc.timeline.trimStart(0, 0.5, 1.0);
            const auto out = render(m, sc);
            writeWav(out, sc.name);
            const auto on = onsets(out);
            expect(! on.empty(), "a sound");
            if (! on.empty()) expectWithinAbsoluteError(on[0], 0.25, 0.006, "it starts half a beat (0.25 s) in");
            const double f = pitchAt(out, (int) (0.30 * kSr));
            note(sc.name, "first sound " + juce::String(on.empty() ? -1.0 : on[0], 3) + " s, pitch " + juce::String(f, 1) + ", level at 0.3 s " + juce::String(rmsOf(out, 0.3, 0.05), 3));
            expectWithinAbsoluteError(f / kTones[0], 1.0, 0.02, "the slice's pitch is unchanged");
            expectEquals(rmsOf(out, 0.55, 0.3), 0.0, "and it ends where the slice does (0.5 s of the loop)");
        }

        beginTest("retime: one beat of slice stretched over two keeps its pitch");
        {
            Scenario sc; sc.name = "retime_x2"; sc.settings.mode = TempoMode::Match; sc.settings.stretchMix = 1.0f;
            sc.timeline.place(0, 0.0, 0.0);
            sc.timeline.retime(0, 0.0, 2.0, 1.0);
            sc.seconds = 2.0;
            const auto out = render(m, sc);
            writeWav(out, sc.name);
            const double f = pitchAt(out, (int) (0.05 * kSr));
            note(sc.name, "pitch " + juce::String(f, 1) + ", level at 0.9 s " + juce::String(rmsOf(out, 0.9, 0.05), 4) + ", after 1.1 s " + juce::String(rmsOf(out, 1.15, 0.3), 4));
            expectWithinAbsoluteError(f / kTones[0], 1.0, 0.03, "pitch kept");
            expectGreaterThan(rmsOf(out, 0.6, 0.3), 0.003, "still sounding in the second half of its time");
            expectLessThan(rmsOf(out, 1.15, 0.3), 0.002, "and finished by the end of its two beats");
        }

        beginTest("a block made longer than its slice runs on into the next one");
        {
            Scenario sc; sc.name = "extend_into_next"; sc.settings.mode = TempoMode::Off;
            sc.timeline.place(0, 0.0, 0.0);
            sc.timeline.setBlockLength(0, 2.0, 0.0);
            const auto out = render(m, sc);
            writeWav(out, sc.name);
            const double f1 = pitchAt(out, (int) (0.10 * kSr)), f2 = pitchAt(out, (int) (0.55 * kSr));
            note(sc.name, "pitch at 0.1 s " + juce::String(f1, 1) + ", at 0.55 s " + juce::String(f2, 1));
            expectWithinAbsoluteError(f1 / kTones[0], 1.0, 0.01, "the slice");
            expectWithinAbsoluteError(f2 / kTones[1], 1.0, 0.01, "then the audio after it");
        }

        beginTest("a cut mid-tone does not click");
        {
            Scenario sc; sc.name = "cut_no_click"; sc.settings.mode = TempoMode::Off;
            sc.timeline.place(0, 0.0, 0.0);
            sc.timeline.setBlockLength(0, 0.3, 0.0);       // cut 0.15 s in, mid-tone
            const auto out = render(m, sc);
            writeWav(out, sc.name);
            const double step = maxStep(out, 0.14, 0.17);
            note(sc.name, "largest sample step across the cut " + juce::String(step, 4));
            expectLessThan(step, 0.05, "no step bigger than the tone's own slope");
        }

        beginTest("reverse: the hit plays backwards, so its level grows");
        {
            Scenario sc; sc.name = "reverse_hit"; sc.settings.mode = TempoMode::Off;
            SliceSettings own; own.reverse = true;
            sc.timeline.place(0, 0.0, 0.0, own);
            const auto out = render(m, sc);
            writeWav(out, sc.name);
            const double early = rmsOf(out, 0.02, 0.1), late = rmsOf(out, 0.38, 0.1);
            note(sc.name, "level early " + juce::String(early, 4) + ", late " + juce::String(late, 4) + ", pitch " + juce::String(pitchAt(out, (int) (0.3 * kSr)), 1));
            expectGreaterThan(late, early * 2.0, "louder at the end");
        }

        beginTest("layer pitch: +12 semitones doubles every slice's pitch");
        {
            Scenario sc; sc.name = "layer_pitch_12"; sc.settings.mode = TempoMode::Off; sc.settings.pitchSemi = 12.0f; allBeats(sc);
            sc.seconds = 2.0;
            const auto out = render(m, sc);
            writeWav(out, sc.name);
            const double f = pitchAt(out, (int) (0.03 * kSr));
            note(sc.name, "slice 1 pitch " + juce::String(f, 1) + " (220 -> 440 expected)");
            expectWithinAbsoluteError(f / 440.0, 1.0, 0.01, "an octave up");
        }

        beginTest("layer filter: a low-pass below the tone takes its level down");
        {
            Scenario open; open.name = "filter_open"; open.settings.mode = TempoMode::Off; open.timeline.place(3, 0.0, 0.0);
            Scenario shut; shut.name = "filter_lowpass_300"; shut.settings.mode = TempoMode::Off; shut.timeline.place(3, 0.0, 0.0);
            shut.settings.filterType = 1; shut.settings.cutoffHz = 300.0f;
            const auto a = render(m, open), b = render(m, shut);
            writeWav(b, shut.name);
            const double db = 20.0 * std::log10((rmsOf(b, 0.02, 0.2) + 1.0e-9) / (rmsOf(a, 0.02, 0.2) + 1.0e-9));
            note(shut.name, "550 Hz tone through a 300 Hz low-pass: " + juce::String(db, 1) + " dB");
            expectLessThan(db, -6.0, "at least 6 dB down");
        }

        report = report.trimEnd().trimCharactersAtEnd(",") + "\n]\n";
        outDir().getChildFile("report.json").replaceWithText(report);
    }
};

static AuditionTests auditionTests;
