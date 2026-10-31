// Loading a loop file and cutting it into slices: a WAV decodes to the file's own channels and rate, a file over the
// byte cap is rejected with the status-bar message and nothing decoded, a missing file says so, and detection on a
// loaded loop gives slices that cover it (Even divisions, and onsets with a pinned marker kept).

#include "Audio/LoopLoader.h"
#include "MuLimits.h"
#include "Sequencer/PhraseAnalysis.h"

#include <juce_core/juce_core.h>

#include <cmath>

using namespace mu_tilate;

namespace
{
constexpr double kSr = 48000.0;

// Writes `frames` of mono or stereo 16-bit audio (a burst every half second) to a WAV in `dir`.
juce::File writeLoop(const juce::File& dir, const juce::String& name, int frames, int channels, double rate = kSr)
{
    juce::AudioBuffer<float> b(channels, frames);
    b.clear();
    juce::Random rng(99);
    for (int t = 0; t * (int) (0.5 * rate) < frames; ++t)
    {
        const int s0 = t * (int) (0.5 * rate);
        for (int i = 0; s0 + i < frames && i < (int) (0.3 * rate); ++i)
            for (int c = 0; c < channels; ++c)
                b.setSample(c, s0 + i, 0.8f * std::exp(-(float) i / (0.02f * (float) rate)) * (rng.nextFloat() * 2.0f - 1.0f));
    }
    auto file = dir.getChildFile(name);
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions {}.withSampleRate(rate).withNumChannels(channels).withBitsPerSample(16));
    if (writer != nullptr) writer->writeFromAudioSampleBuffer(b, 0, frames);
    return file;
}
}

struct PhraseLoadTests : public juce::UnitTest
{
    PhraseLoadTests() : juce::UnitTest("Phrase loading and detection", "mu-tilate") {}

    void runTest() override
    {
        const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mu-tilate-load-tests");
        dir.createDirectory();

        beginTest("a WAV decodes to its own channels and sample rate");
        {
            const auto f = writeLoop(dir, "stereo.wav", 48000, 2, 44100.0);
            const auto l = mu_core::loadLoop(f, mu_limits::kMaxLoopBytes);
            expect(l.ok(), l.error);
            expectEquals(l.audio->getNumChannels(), 2);
            expectEquals(l.audio->getNumSamples(), 48000);
            expectEquals(l.sampleRate, 44100.0);
            expectEquals(l.name, juce::String("stereo"));
            const auto m = mu_core::loadLoop(writeLoop(dir, "mono.wav", 24000, 1), mu_limits::kMaxLoopBytes);
            expectEquals(m.audio->getNumChannels(), 1);                // a mono file stays mono
        }

        beginTest("a file over the byte cap is rejected before decoding, with the status message");
        {
            const auto f = writeLoop(dir, "long.wav", 96000, 2);        // 2 s stereo = 768000 bytes
            const auto l = mu_core::loadLoop(f, 400000);
            expect(! l.ok());
            expect(l.error.startsWith("Loop too long: 2.0 s, limit 1.0 s"), l.error);
        }

        beginTest("a missing or unreadable file says so");
        {
            expect(mu_core::loadLoop(dir.getChildFile("nope.wav"), 1000000).error.startsWith("File not found"));
            const auto junk = dir.getChildFile("junk.wav");
            junk.replaceWithText("not audio");
            expect(mu_core::loadLoop(junk, 1000000).error.startsWith("Cannot read"));
        }

        beginTest("detection on a loaded loop: onsets cover it, Even divides it, a pinned marker survives");
        {
            const auto l = mu_core::loadLoop(writeLoop(dir, "bursts.wav", 96000, 1), mu_limits::kMaxLoopBytes);
            expect(l.ok());
            DetectSettings s;
            const auto curve = analyseLoop(*l.audio, l.sampleRate, s);
            expect(! curve.empty());
            auto list = detectSlices(curve, *l.audio, s);
            expectGreaterOrEqual(list.size(), 3);                       // bursts at 0, 0.5, 1.0, 1.5 s
            expectEquals(list.slice(list.size() - 1).end, 96000);

            list.addMarker(10000);                                      // hand-placed: pinned
            const auto again = detectSlices(curve, *l.audio, s, &list);
            expect(again.indexAt(10000) >= 0 && again.marker(again.indexAt(10000)).position == 10000);

            DetectSettings even; even.method = 2; even.evenParts = 8;
            expectEquals(detectSlices({}, *l.audio, even).size(), 8);
            expect(analyseLoop(*l.audio, l.sampleRate, even).empty());
        }

        beginTest("resampling keeps the pitch, the level and the length ratio");
        {
            const int n = 44100;
            juce::AudioBuffer<float> src(2, n);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i) src.setSample(c, i, 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * 1000.0f * (float) i / 44100.0f));
            const auto out = mu_core::resampleLoop(src, 44100.0, 48000.0);
            expectEquals(out->getNumChannels(), 2);
            expectEquals(out->getNumSamples(), 48000);
            int zc = 0;
            for (int i = 4001; i < 44000; ++i)
                if ((out->getSample(0, i - 1) < 0.0f) != (out->getSample(0, i) < 0.0f)) ++zc;
            expectGreaterOrEqual(zc, 2 * 1000 * 39999 / 48000 - 2);      // 1 kHz stays 1 kHz
            expectLessOrEqual(zc, 2 * 1000 * 39999 / 48000 + 2);
            expectWithinAbsoluteError(out->getMagnitude(0, 4000, 40000), 0.5f, 0.02f);
        }

        dir.deleteRecursively();
    }
};

static PhraseLoadTests phraseLoadTests;
