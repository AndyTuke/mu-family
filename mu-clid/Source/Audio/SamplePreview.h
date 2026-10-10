#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

// Encapsulates the sample-preview player used by the file browser (SampleBrowserContent).
// Routes a decoded audio file through a transport and mixes it at 0.7x into whatever
// master bus buffer is passed to mixInto(). Completely message-thread-owned for start/stop;
// mixInto() is called on the audio thread.
class SamplePreview
{
public:
    SamplePreview();

    void prepare(int blockSize, double sampleRate);
    void releaseResources();

    // Message-thread: begin/end playback of a file.
    void start(const juce::File& file);
    void stop();

    // Audio thread: mix preview output into masterBus if currently playing.
    void mixInto(juce::AudioBuffer<float>& masterBus, int numSamples);

private:
    juce::AudioFormatManager                          formatManager;
    juce::AudioTransportSource                        transport;
    std::unique_ptr<juce::AudioFormatReaderSource>    source;
    juce::AudioBuffer<float>                          scratchBuffer;
};
