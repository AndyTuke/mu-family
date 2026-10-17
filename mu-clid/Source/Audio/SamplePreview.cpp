#include "SamplePreview.h"

namespace mu_clid {

SamplePreview::SamplePreview()
{
    formatManager.registerBasicFormats();
}

SamplePreview::~SamplePreview() = default;

void SamplePreview::prepare(int blockSize, double sr)
{
    sampleRate = sr;
    chunkMax = blockSize;
}

void SamplePreview::releaseResources()
{
    stop();
}

void SamplePreview::collectRetired()
{
    // Keep the published buffer and whichever one the audio thread is reading; free the rest.
    const Decoded* live = current.load();
    const Decoded* busy = audioHolding.load();
    for (size_t i = owned.size(); i-- > 0; )
        if (owned[i].get() != live && owned[i].get() != busy)
            owned.erase(owned.begin() + (std::ptrdiff_t) i);
}

void SamplePreview::start(const juce::File& file)
{
    if (!file.existsAsFile()) return;
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (!reader || reader->lengthInSamples <= 0) return;
    // Phase 1: decode up to kMaxSeconds at the file's own rate (mono is duplicated to both channels, >2 channels use the first two).
    const double srcRate = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
    const int srcLen = (int) juce::jmin<juce::int64>(reader->lengthInSamples, (juce::int64) (kMaxSeconds * srcRate));
    juce::AudioBuffer<float> src(2, srcLen);
    src.clear();
    reader->read(&src, 0, srcLen, 0, true, true);
    if (reader->numChannels == 1)
        src.copyFrom(1, 0, src, 0, 0, srcLen);
    // Phase 2: convert to the device rate with Lagrange interpolation, as the transport's resampler did.
    auto dec = std::make_unique<Decoded>();
    if (sampleRate > 0.0 && std::abs(srcRate - sampleRate) > 1.0e-6)
    {
        const double ratio = srcRate / sampleRate;
        const int outLen = (int) std::ceil((double) srcLen / ratio);
        dec->audio.setSize(2, outLen);
        dec->audio.clear();
        for (int ch = 0; ch < 2; ++ch)
        {
            juce::LagrangeInterpolator interp;
            interp.process(ratio, src.getReadPointer(ch), dec->audio.getWritePointer(ch), outLen);
        }
    }
    else
        dec->audio = std::move(src);
    dec->id = nextId++;
    // Phase 3: publish; the audio thread restarts from zero when it sees the new id.
    Decoded* raw = dec.get();
    owned.push_back(std::move(dec));
    current.store(raw);
    collectRetired();
}

void SamplePreview::stop()
{
    current.store(nullptr);
    collectRetired();
}

void SamplePreview::mixInto(juce::AudioBuffer<float>& masterBus, int numSamples)
{
    // Take a hazard pointer so the message thread does not free the buffer mid-read.
    Decoded* d = nullptr;
    for (;;)
    {
        d = current.load();
        audioHolding.store(d);
        if (current.load() == d) break;
    }
    if (d == nullptr || chunkMax <= 0) { audioHolding.store(nullptr); return; }

    if (d->id != playingId) { playingId = d->id; readPos = 0; }

    // Chunk to the prepared size, copying from the in-memory buffer.
    const int len = d->audio.getNumSamples();
    for (int done = 0; done < numSamples && readPos < len; )
    {
        const int n = juce::jmin(chunkMax, numSamples - done, len - readPos);
        for (int ch = 0; ch < masterBus.getNumChannels(); ++ch)
            masterBus.addFrom(ch, done, d->audio, ch % 2, readPos, n, 0.7f);
        readPos += n;
        done += n;
    }
    audioHolding.store(nullptr);
}

} // namespace mu_clid


