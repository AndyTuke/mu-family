#include "Audio/LoopLoader.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace mu_core
{

std::shared_ptr<const juce::AudioBuffer<float>> resampleLoop(const juce::AudioBuffer<float>& src, double fromRate, double toRate)
{
    const int n = src.getNumSamples();
    const int outN = (int) std::ceil((double) n * toRate / fromRate);
    auto out = std::make_shared<juce::AudioBuffer<float>>(src.getNumChannels(), outN);
    out->clear();
    if (n <= 0 || fromRate <= 0.0 || toRate <= 0.0) return out;

    // The interpolator reads a window of input around each output sample, so the input is padded with silence at the end.
    const int pad = 256;
    std::vector<float> in((size_t) n + (size_t) pad, 0.0f);
    for (int c = 0; c < src.getNumChannels(); ++c)
    {
        std::copy(src.getReadPointer(c), src.getReadPointer(c) + n, in.begin());
        std::fill(in.begin() + n, in.end(), 0.0f);
        juce::WindowedSincInterpolator interp;
        interp.process(fromRate / toRate, in.data(), out->getWritePointer(c), outN);
    }
    return out;
}

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
