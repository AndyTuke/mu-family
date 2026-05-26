#include "SamplePreview.h"

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
    scratchBuffer.clear(0, 0, numSamples);
    scratchBuffer.clear(1, 0, numSamples);
    transport.getNextAudioBlock({ &scratchBuffer, 0, numSamples });
    for (int ch = 0; ch < masterBus.getNumChannels(); ++ch)
        masterBus.addFrom(ch, 0, scratchBuffer,
                          ch % scratchBuffer.getNumChannels(), 0, numSamples, 0.7f);
}
