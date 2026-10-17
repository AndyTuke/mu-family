#include "SamplePreview.h"

namespace mu_clid {

SamplePreview::SamplePreview()
{
    formatManager.registerBasicFormats();
}

void SamplePreview::prepare(int blockSize, double sampleRate)
{
    transport.prepareToPlay(blockSize, sampleRate);
    scratchBuffer.setSize(2, blockSize, false, true, true);
}

void SamplePreview::releaseResources()
{
    transport.releaseResources();
}

void SamplePreview::start(const juce::File& file)
{
    if (!file.existsAsFile()) return;
    auto* reader = formatManager.createReaderFor(file);
    if (!reader) return;
    transport.stop();
    transport.setSource(nullptr);
    source = std::make_unique<juce::AudioFormatReaderSource>(reader, true);
    transport.setSource(source.get(), 0, nullptr, reader->sampleRate, reader->numChannels);
    transport.setPosition(0.0);
    transport.start();
}

void SamplePreview::stop()
{
    transport.stop();
    transport.setSource(nullptr);
    source.reset();
}

void SamplePreview::mixInto(juce::AudioBuffer<float>& masterBus, int numSamples)
{
    if (!transport.isPlaying()) return;

    // Chunk to the prepared size: a host block larger than prepare() would otherwise run past the scratch buffer.
    const int chunkMax = scratchBuffer.getNumSamples();
    if (chunkMax <= 0) return;
    for (int done = 0; done < numSamples; )
    {
        const int n = juce::jmin(chunkMax, numSamples - done);
        scratchBuffer.clear(0, 0, n);
        scratchBuffer.clear(1, 0, n);
        transport.getNextAudioBlock({ &scratchBuffer, 0, n });
        for (int ch = 0; ch < masterBus.getNumChannels(); ++ch)
            masterBus.addFrom(ch, done, scratchBuffer,
                              ch % scratchBuffer.getNumChannels(), 0, n, 0.7f);
        done += n;
    }
}

} // namespace mu_clid
