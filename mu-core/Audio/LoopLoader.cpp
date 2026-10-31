#include "Audio/LoopLoader.h"

#include <cmath>

namespace mu_core
{

LoadedLoop loadLoop(const juce::File& file, long long maxBytes)
{
    LoadedLoop out;
    out.name = file.getFileNameWithoutExtension();

    if (! file.existsAsFile()) { out.error = "File not found: " + file.getFileName(); return out; }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->numChannels == 0 || reader->sampleRate <= 0.0)
    {
        out.error = "Cannot read " + file.getFileName();
        return out;
    }

    // The cap is in bytes of the decoded float32 data, checked before any of it is read.
    const long long bytes = reader->lengthInSamples * (long long) reader->numChannels * 4;
    if (bytes > maxBytes)
    {
        const double seconds = (double) reader->lengthInSamples / reader->sampleRate;
        const double limit   = (double) maxBytes / (4.0 * (double) reader->numChannels * reader->sampleRate);
        out.error = "Loop too long: " + juce::String(seconds, 1) + " s, limit " + juce::String(limit, 1) + " s for this file";
        return out;
    }

    auto buffer = std::make_shared<juce::AudioBuffer<float>>((int) reader->numChannels, (int) reader->lengthInSamples);
    if (! reader->read(buffer.get(), 0, (int) reader->lengthInSamples, 0, true, true))
    {
        out.error = "Cannot read " + file.getFileName();
        return out;
    }

    out.audio = std::move(buffer);
    out.sampleRate = reader->sampleRate;
    return out;
}

} // namespace mu_core
