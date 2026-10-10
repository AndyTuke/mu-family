#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

// How a preset carries a rhythm's sample inside its file. Integer PCM WAV / AIFF (up to 24 bit,
// 8 channels) is re-encoded as FLAC — lossless, so the decoded audio is bit-identical, and the
// preset is usually 40–60 % smaller. Anything else (float data, 32-bit, MP3 / OGG / FLAC sources,
// or a file FLAC wouldn't shrink) is stored as the original file bytes, as presets always were.
// On load a FLAC sample is decoded back into a file of its original type and name, so the rest
// of the sample path sees exactly what was embedded. Sample metadata (cue / loop chunks) is not
// kept in the FLAC path; mu-Clid reads only the audio.
namespace mu_clid::embedded_sample
{

// The embedded form: `codec` is "" for raw file bytes or "flac"; `bits` is the source bit depth
// (restored on decode).
struct Encoded
{
    juce::MemoryBlock data;
    juce::String      codec;
    int               bits = 0;
};

inline bool isFlacCandidate(const juce::File& f)
{
    return f.hasFileExtension("wav;aif;aiff");
}

// Encode `f` for embedding. Returns empty data only when the file can't be read.
inline Encoded encode(const juce::File& f)
{
    Encoded raw;
    if (! f.loadFileAsData(raw.data) || raw.data.getSize() == 0)
        return {};
    if (! isFlacCandidate(f))
        return raw;

    // Read the source as integer PCM; only depths FLAC holds exactly are re-encoded.
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(f));
    if (reader == nullptr || reader->usesFloatingPointData || reader->bitsPerSample > 24
        || reader->numChannels < 1 || reader->numChannels > 8 || reader->lengthInSamples <= 0)
        return raw;

    // Write FLAC to memory through the integer path (writeFromAudioReader keeps ints exact).
    juce::FlacAudioFormat flac;
    juce::MemoryBlock flacData;
    {
        std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::MemoryOutputStream>(flacData, false);
        auto writer = flac.createWriterFor(out, juce::AudioFormatWriterOptions{}
                                                    .withSampleRate(reader->sampleRate)
                                                    .withNumChannels((int) reader->numChannels)
                                                    .withBitsPerSample(reader->bitsPerSample <= 16 ? 16 : 24)
                                                    .withQualityOptionIndex(flac.getQualityOptions().size() - 1));
        if (writer == nullptr || ! writer->writeFromAudioReader(*reader, 0, -1))
            return raw;
    }   // the writer flushes and finishes the stream here

    if (flacData.getSize() == 0 || flacData.getSize() >= raw.data.getSize())
        return raw;   // FLAC would not save anything

    Encoded e;
    e.data  = std::move(flacData);
    e.codec = "flac";
    e.bits  = (int) reader->bitsPerSample;
    return e;
}

// Write an embedded sample to `dest`: raw bytes as they are, FLAC decoded back into dest's own
// format (WAV / AIFF, by its extension) at the stored bit depth. False on any failure.
inline bool decodeTo(const juce::MemoryBlock& data, const juce::String& codec, int bits, const juce::File& dest)
{
    if (data.getSize() == 0)
        return false;
    if (codec.isEmpty())
        return dest.replaceWithData(data.getData(), data.getSize());
    if (codec != "flac")
        return false;   // written by a newer build with a codec this one doesn't know

    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::AudioFormatReader> reader(
        flac.createReaderFor(new juce::MemoryInputStream(data, false), true));
    if (reader == nullptr)
        return false;

    std::unique_ptr<juce::AudioFormat> target;
    if (dest.hasFileExtension("wav"))           target = std::make_unique<juce::WavAudioFormat>();
    else if (dest.hasFileExtension("aif;aiff")) target = std::make_unique<juce::AiffAudioFormat>();
    else                                        return false;

    juce::TemporaryFile temp(dest);
    {
        std::unique_ptr<juce::OutputStream> out = temp.getFile().createOutputStream();
        if (out == nullptr)
            return false;
        auto writer = target->createWriterFor(out, juce::AudioFormatWriterOptions{}
                                                       .withSampleRate(reader->sampleRate)
                                                       .withNumChannels((int) reader->numChannels)
                                                       .withBitsPerSample(bits > 0 ? bits : (int) reader->bitsPerSample));
        if (writer == nullptr || ! writer->writeFromAudioReader(*reader, 0, -1))
            return false;
    }
    return temp.overwriteTargetFileWithTemporary();
}

} // namespace mu_clid::embedded_sample
