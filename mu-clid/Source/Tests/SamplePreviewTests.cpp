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

    // A 1 s stereo 440 Hz tone (0.5 and 0.4 on the two channels, so the channels are told apart) as a 16-bit WAV,
    // written by hand (a 44-byte RIFF header plus interleaved samples) so the test does not depend on a WAV writer.
    juce::File writeTone(const juce::File& dir)
    {
        auto file = dir.getChildFile("preview_tone.wav");
        file.deleteFile();
        const int frames = (int) kSr;
        const int dataBytes = frames * 2 * 2;
        juce::MemoryOutputStream m;
        m.write("RIFF", 4);  m.writeInt(36 + dataBytes);  m.write("WAVEfmt ", 8);
        m.writeInt(16);  m.writeShort(1);  m.writeShort(2);
        m.writeInt((int) kSr);  m.writeInt((int) kSr * 4);  m.writeShort(4);  m.writeShort(16);
        m.write("data", 4);  m.writeInt(dataBytes);
        for (int i = 0; i < frames; ++i)
        {
            const float s = (float) std::sin(6.283185307179586 * 440.0 * (double) i / kSr);
            m.writeShort((short) juce::roundToInt(0.5f * s * 32767.0f));
            m.writeShort((short) juce::roundToInt(0.4f * s * 32767.0f));
        }
        const bool ok = file.replaceWithData(m.getData(), m.getDataSize()); return ok ? file : juce::File();
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
        beginTest("setup");   // expect() needs a current test
        const auto tone = writeTone(dir); expect(tone.existsAsFile(), "the test tone was written");
        if (! tone.existsAsFile()) { dir.deleteRecursively(); return; }

        // A block 2x, 5x and 5x plus a ragged tail the prepared size: the last chunk is shorter than the others.
        for (const int total : { 2 * kPrepared, 5 * kPrepared, 5 * kPrepared + 37 })
        {
            beginTest("mixInto of " + juce::String(total) + " samples (prepared " + juce::String(kPrepared)
                      + ") equals the same audio in prepared-size blocks");

            // Reference: the audio the preview produces when the host honours the prepared size.
            mu_clid::SamplePreview reference; reference.prepare(kPrepared, kSr);
            reference.start(tone); juce::AudioBuffer<float> expected(2, total);
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

struct SamplePreviewDecodeTests : juce::UnitTest
{
    SamplePreviewDecodeTests() : juce::UnitTest("mu-Clid SamplePreview plays from memory", "mu-clid") {}

    void runTest() override
    {
        const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("mu-preview-test-" + juce::String::toHexString(juce::Random().nextInt64()));
        dir.createDirectory();
        beginTest("setup");   // expect() needs a current test
        const auto tone = writeTone(dir); if (! tone.existsAsFile()) { expect(false, "the test tone was written"); dir.deleteRecursively(); return; }

        // Reference: the file decoded directly, scaled by the preview's 0.7 mix gain.
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(tone));
        const int len = (int) reader->lengthInSamples;
        juce::AudioBuffer<float> ref(2, len);
        reader->read(&ref, 0, len, 0, true, true);
        reader.reset();

        mu_clid::SamplePreview p;
        p.prepare(kPrepared, kSr);
        p.start(tone);
        tone.deleteFile();   // any file read in mixInto would now fail: playback must come from memory

        beginTest("preview matches the decoded file at 0.7x with the file deleted after start()");
        const int total = len + 3 * kPrepared;
        juce::AudioBuffer<float> out(2, total);
        out.clear();
        p.mixInto(out, total);   // oversize block, also runs past the end of the file
        float maxDiff = 0.0f, tailLevel = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < total; ++i)
            {
                const float e = i < len ? 0.7f * ref.getSample(ch, i) : 0.0f;
                maxDiff = juce::jmax(maxDiff, std::abs(out.getSample(ch, i) - e));
                if (i >= len) tailLevel = juce::jmax(tailLevel, std::abs(out.getSample(ch, i)));
            }
        expect(maxDiff < 1.0e-6f, "largest difference from the reference " + juce::String(maxDiff, 8));
        expect(tailLevel == 0.0f, "silent after the end of the file");

        beginTest("stop() silences and start() restarts from the beginning");
        const auto tone2 = writeTone(dir);
        p.start(tone2);
        juce::AudioBuffer<float> a(2, kPrepared), b(2, kPrepared), c(2, kPrepared);
        a.clear(); b.clear(); c.clear();
        p.mixInto(a, kPrepared);
        p.stop();
        p.mixInto(b, kPrepared);
        expect(b.getMagnitude(0, kPrepared) == 0.0f, "silent after stop()");
        p.start(tone2);
        p.start(tone2);   // restart while a preview is already published
        p.mixInto(c, kPrepared);
        float diff = 0.0f;
        for (int i = 0; i < kPrepared; ++i)
            diff = juce::jmax(diff, std::abs(a.getSample(0, i) - c.getSample(0, i)));
        expect(a.getMagnitude(0, kPrepared) > 0.05f, "played before stop()");
        expect(diff == 0.0f, "the restart begins at the start of the file");
        p.stop();

        dir.deleteRecursively();
    }
};

static SamplePreviewDecodeTests samplePreviewDecodeTests;













