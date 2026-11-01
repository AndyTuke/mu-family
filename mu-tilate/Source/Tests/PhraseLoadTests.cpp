// Cutting a loaded loop into slices (loading itself is tested in mu-core, LoopLoaderTests): detection on a loaded loop gives
// slices that cover it (Even divisions, and onsets with a pinned marker kept).

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

        dir.deleteRecursively();
    }
};

static PhraseLoadTests phraseLoadTests;
