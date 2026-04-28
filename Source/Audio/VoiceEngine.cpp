#include "VoiceEngine.h"

VoiceEngine::VoiceEngine()
{
    formatManager.registerBasicFormats();
}

void VoiceEngine::prepareToPlay(double sampleRate, int /*blockSize*/)
{
    currentSampleRate = sampleRate;
    juce::ScopedWriteLock sl(bufferLock);
    playbackRatio = (buffer.getNumSamples() > 0)
                    ? originalSampleRate / currentSampleRate
                    : 1.0;
}

void VoiceEngine::loadFile(const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr)
        return;

    // Load into temp buffer outside the lock (disk I/O should not block audio).
    const int numSamples   = static_cast<int>(reader->lengthInSamples);
    const int numChannels  = static_cast<int>(reader->numChannels);
    juce::AudioBuffer<float> temp(numChannels, numSamples);
    reader->read(&temp, 0, numSamples, 0, true, true);

    const double srcRate = reader->sampleRate;

    // Swap under write lock — O(1), won't stall the audio thread for long.
    {
        juce::ScopedWriteLock sl(bufferLock);
        buffer            = std::move(temp);
        originalSampleRate = srcRate;
        playbackRatio      = srcRate / currentSampleRate;
        sampleLoaded       = true;
    }
}

void VoiceEngine::trigger()
{
    for (auto& v : voices)
    {
        if (!v.isActive())
        {
            v.trigger();
            return;
        }
    }
    // All busy — steal round-robin.
    voices[nextVoice].trigger();
    nextVoice = (nextVoice + 1) % MaxVoices;
}

void VoiceEngine::process(juce::AudioBuffer<float>& output, int numSamples)
{
    juce::ScopedReadLock sl(bufferLock);
    if (!sampleLoaded || buffer.getNumSamples() == 0)
        return;

    for (auto& v : voices)
        v.process(buffer, playbackRatio, output, numSamples);
}

bool VoiceEngine::hasSample() const
{
    return sampleLoaded;
}
