// SamplePreview::mixInto against a host block larger than prepare() promised. The preview reads through a scratch
// buffer sized at prepare(); a larger block used to run past it. mixInto now works in chunks of at most the prepared
// size, so an oversize call must give exactly what the same audio rendered in prepared-size blocks gives. A write past
// the scratch buffer is not visible in a normal build (it is undefined behaviour); run this suite under an address
// sanitiser or the debug heap to catch that part. The equality check below catches a chunking that drops or repeats audio.

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <memory>

#include "Audio/SamplePreview.h"

namespace
{
    constexpr double kSr = 48000.0;
    constexpr int kPrepared = 256;

    // A 1 s stereo 440 Hz tone (0.5 and 0.4 on the two channels, so the channels are told apart) as a 16-bit WAV.
    juce::File writeTone(const juce::File& dir)
    {
        auto file = dir.getChildFile("preview_tone.wav");
        file.deleteFile();
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> out = file.createOutputStream();
            if (out == nullptr) return {};
            auto writer = wav.createWriterFor(out, juce::AudioFormatWriterOptions{}.withSampleRate(kSr)
                                                                                   .withNumChannels(2)
                                                                                   .withBitsPerSample(16));
            if (writer == nullptr) return {};
            juce::AudioBuffer<float> b(2, (int) kSr);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float s = (float) std::sin(6.283185307179586 * 440.0 * (double) i / kSr);
                b.setSample(0, i, 0.5f * s);
                b.setSample(1, i, 0.4f * s);
            }
            writer->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
        }   // the writer is destroyed here, which flushes the file
        return file;
    }
}

struct SamplePreviewTests : juce::UnitTest
{
    SamplePreviewTests() : juce::UnitTest("mu-Clid SamplePreview chunks an oversize host block", "mu-clid") {}

    void runTest() override
    {
        const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("mu-preview-test-" + juce::String::toHexString(juce::Random().nextInt64()));
        dir.createDirectory();
        const auto tone = writeTone(dir);
        expect(tone.existsAsFile(), "the test tone was written");
        if (! tone.existsAsFile()) { dir.deleteRecursively(); return; }

        // A block 2x, 5x and 5x plus a ragged tail the prepared size: the last chunk is shorter than the others.
        for (const int total : { 2 * kPrepared, 5 * kPrepared, 5 * kPrepared + 37 })
        {
            beginTest("mixInto of " + juce::String(total) + " samples (prepared " + juce::String(kPrepared)
                      + ") equals the same audio in prepared-size blocks");

            // Reference: the audio the preview produces when the host honours the prepared size.
            mu_clid::SamplePreview reference;
            reference.prepare(kPrepared, kSr);
            reference.start(tone);
            juce::AudioBuffer<float> expected(2, total);
            expected.clear();
            for (int done = 0; done < total; done += kPrepared)
            {
                const int n = juce::jmin(kPrepared, total - done);
                juce::AudioBuffer<float> window(expected.getArrayOfWritePointers(), 2, done, n);   // a view onto `expected`
                reference.mixInto(window, n);
            }

            // Under test: one call with the whole oversize block.
            mu_clid::SamplePreview oversize;
            oversize.prepare(kPrepared, kSr);
            oversize.start(tone);
            juce::AudioBuffer<float> actual(2, total);
            actual.clear();
            oversize.mixInto(actual, total);

            float level = 0.0f, maxDiff = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < total; ++i)
                {
                    level   = juce::jmax(level, std::abs(expected.getSample(ch, i)));
                    maxDiff = juce::jmax(maxDiff, std::abs(actual.getSample(ch, i) - expected.getSample(ch, i)));
                }
            // Detach the file before the previews go out of scope (the class declares its reader after the transport).
            reference.stop();
            oversize.stop();

            logMessage("level " + juce::String(level, 3) + ", largest difference " + juce::String(maxDiff, 8));
            expect(level > 0.05f, "the preview played (level " + juce::String(level, 3) + ")");
            expect(maxDiff == 0.0f, "the oversize block differs from prepared-size blocks by " + juce::String(maxDiff, 8));

            // The last sample of the block is as audible as the first: nothing was left unwritten at the end.
            float tail = 0.0f;
            for (int i = total - kPrepared; i < total; ++i) tail = juce::jmax(tail, std::abs(actual.getSample(0, i)));
            expect(tail > 0.05f, "the end of the oversize block is filled (level " + juce::String(tail, 3) + ")");
        }

        dir.deleteRecursively();
    }
};

static SamplePreviewTests samplePreviewTests;
