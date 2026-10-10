// MP3 sample support: mu-Clid builds JUCE's MP3 decoder in (JUCE_USE_MP3AUDIOFORMAT), so the
// standard format manager the voice engine uses must open .mp3 files. The stream is built in
// memory from silent MPEG-1 Layer III frames (no encoder or sample file needed).

#include <juce_audio_formats/juce_audio_formats.h>
class Mp3FormatTest : public juce::UnitTest
{
public:
    Mp3FormatTest() : juce::UnitTest ("MP3 sample format", "Audio") {}

    // A silent MPEG-1 Layer III stream: frame header FF FB 90 64 (128 kbit/s, 44.1 kHz, joint
    // stereo, no CRC) + an all-zero side info / main data, which decodes to silence. 417 bytes a frame.
    static juce::MemoryBlock silentMp3(int frames)
    {
        juce::MemoryBlock mb;
        for (int f = 0; f < frames; ++f)
        {
            const juce::uint8 header[] = { 0xFF, 0xFB, 0x90, 0x64 };
            mb.append(header, sizeof(header));
            juce::HeapBlock<char> zeros(417 - 4, true);
            mb.append(zeros.get(), 417 - 4);
        }
        return mb;
    }

    void runTest() override
    {
        beginTest ("The basic formats include MP3");
        {
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            expect (fm.findFormatForFileExtension("mp3") != nullptr, "no MP3 format registered");
            expect (fm.getWildcardForAllFormats().containsIgnoreCase("*.mp3"), "MP3 missing from the file wildcard");
        }

        beginTest ("An MP3 stream opens and decodes");
        {
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            auto* mp3 = fm.findFormatForFileExtension("mp3");
            expect (mp3 != nullptr);
            if (mp3 == nullptr) return;

            const auto data = silentMp3(40);
            std::unique_ptr<juce::AudioFormatReader> reader(
                mp3->createReaderFor(new juce::MemoryInputStream(data, false), true));
            expect (reader != nullptr, "the MP3 reader did not open the stream");
            if (reader == nullptr) return;
            expectEquals (reader->sampleRate, 44100.0);
            expect (reader->lengthInSamples > 0, "no decoded length");

            juce::AudioBuffer<float> buf((int) reader->numChannels, 4096);
            expect (reader->read(&buf, 0, 4096, 0, true, true), "read failed");
            expect (buf.getMagnitude(0, 4096) < 1.0e-4f, "silent frames decoded to non-silence");
        }
    }
};

static Mp3FormatTest mp3FormatTest;
