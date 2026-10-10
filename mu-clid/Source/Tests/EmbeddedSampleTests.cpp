// Embedded preset samples: integer WAV is stored as FLAC and decodes back sample-identical;
// float WAV and other files stay as their raw bytes; an older preset (raw, no codec) still loads.

#include <juce_audio_formats/juce_audio_formats.h>
#include "Persistence/EmbeddedSample.h"

using namespace mu_clid;

class EmbeddedSampleTest : public juce::UnitTest
{
public:
    EmbeddedSampleTest() : juce::UnitTest ("Embedded preset sample", "Persistence") {}

    // A drum-like test sample: a decaying 110 Hz tone with a little deterministic noise.
    static juce::AudioBuffer<float> testAudio(int channels, int length)
    {
        juce::AudioBuffer<float> b(channels, length);
        juce::Random rng(1234);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < length; ++i)
            {
                const float env = std::exp(-3.0f * (float) i / (float) length);
                b.setSample(ch, i, 0.7f * env * std::sin(juce::MathConstants<float>::twoPi * 110.0f * (float) i / 48000.0f)
                                   + 0.002f * (rng.nextFloat() - 0.5f));
            }
        return b;
    }

    static bool writeWav(const juce::File& f, const juce::AudioBuffer<float>& audio, int bits, bool asFloat)
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> out = f.createOutputStream();
        if (out == nullptr) return false;
        auto opts = juce::AudioFormatWriterOptions{}.withSampleRate(48000.0)
                        .withNumChannels(audio.getNumChannels()).withBitsPerSample(bits);
        if (asFloat) opts = opts.withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
        auto w = wav.createWriterFor(out, opts);
        return w != nullptr && w->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
    }

    // Every sample of two files, read as integers, matches.
    bool identical(const juce::File& a, const juce::File& b)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> ra(fm.createReaderFor(a)), rb(fm.createReaderFor(b));
        if (ra == nullptr || rb == nullptr || ra->lengthInSamples != rb->lengthInSamples
            || ra->numChannels != rb->numChannels || ra->bitsPerSample != rb->bitsPerSample)
            return false;
        const int n = (int) ra->lengthInSamples, chans = (int) ra->numChannels;
        juce::HeapBlock<int> da((size_t) (n * chans)), db((size_t) (n * chans));
        std::vector<int*> pa, pb;
        for (int c = 0; c < chans; ++c) { pa.push_back(da + c * n); pb.push_back(db + c * n); }
        pa.push_back(nullptr); pb.push_back(nullptr);
        ra->read(pa.data(), chans, 0, n, false);
        rb->read(pb.data(), chans, 0, n, false);
        return std::memcmp(da.get(), db.get(), sizeof(int) * (size_t) (n * chans)) == 0;
    }

    void runTest() override
    {
        const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("muClid_embed_test_" + juce::String(juce::Random::getSystemRandom().nextInt()));
        dir.createDirectory();
        const auto audio = testAudio(2, 48000);

        for (const int bits : { 16, 24 })
        {
            beginTest ("A " + juce::String(bits) + "-bit WAV embeds as FLAC and decodes sample-identical");
            const auto src = dir.getChildFile("kick" + juce::String(bits) + ".wav");
            expect (writeWav(src, audio, bits, false));
            const auto e = mu_clid::embedded_sample::encode(src);
            expectEquals (e.codec, juce::String("flac"));
            expectEquals (e.bits, bits);
            const auto saving = 1.0 - (double) e.data.getSize() / (double) src.getSize();
            logMessage ("  " + juce::String(bits) + "-bit: " + juce::String(src.getSize()) + " -> "
                        + juce::String((juce::int64) e.data.getSize()) + " bytes (" + juce::String(saving * 100.0, 1) + " % smaller)");
            expect (saving > 0.2, "FLAC saved less than 20 %");

            const auto back = dir.getChildFile("back" + juce::String(bits) + ".wav");
            expect (mu_clid::embedded_sample::decodeTo(e.data, e.codec, e.bits, back), "decode failed");
            expect (identical(src, back), "decoded audio differs from the source");
        }

        beginTest ("A float WAV stays as its raw bytes");
        {
            const auto src = dir.getChildFile("float.wav");
            expect (writeWav(src, audio, 32, true));
            const auto e = mu_clid::embedded_sample::encode(src);
            expect (e.codec.isEmpty(), "float data must not be re-encoded");
            expectEquals ((juce::int64) e.data.getSize(), src.getSize());
        }

        beginTest ("An older preset's raw bytes still load");
        {
            const auto src = dir.getChildFile("old.wav");
            expect (writeWav(src, audio, 16, false));
            juce::MemoryBlock raw;
            src.loadFileAsData(raw);
            const auto back = dir.getChildFile("old_back.wav");
            expect (mu_clid::embedded_sample::decodeTo(raw, {}, 0, back));
            expect (back.loadFileAsData(raw) && back.getSize() == src.getSize() && identical(src, back));
            expect (! mu_clid::embedded_sample::decodeTo(raw, "opus", 0, dir.getChildFile("x.wav")),
                    "an unknown codec is refused, not written as garbage");
        }

        dir.deleteRecursively();
    }
};

static EmbeddedSampleTest embeddedSampleTest;
