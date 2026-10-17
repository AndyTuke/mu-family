#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>
#include <memory>
#include <vector>

namespace mu_clid {

// Sample-preview player used by the file browser (SampleBrowserContent). start() decodes the whole file (capped at
// kMaxSeconds) into memory on the message thread, converted to the prepared sample rate; mixInto() on the audio thread
// only copies from that buffer at 0.7x, so it does no file I/O, allocation or locking.
class SamplePreview
{
public:
    SamplePreview();
    ~SamplePreview();

    void prepare(int blockSize, double sampleRate);
    void releaseResources();

    // Message-thread: begin/end playback of a file. Safe while the audio thread is mixing.
    void start(const juce::File& file);
    void stop();

    // Audio thread: mix preview output into masterBus if currently playing.
    void mixInto(juce::AudioBuffer<float>& masterBus, int numSamples);

    static constexpr double kMaxSeconds = 30.0;

private:
    struct Decoded
    {
        juce::AudioBuffer<float> audio;   // 2 channels at the prepared sample rate
        juce::uint64             id = 0;  // unique per decode, so a recycled address is never mistaken for the same preview
    };

    // Message thread: free retired buffers the audio thread is no longer reading.
    void collectRetired();

    juce::AudioFormatManager formatManager;
    double                   sampleRate = 0.0;
    int                      chunkMax = 0;

    std::atomic<Decoded*>    current { nullptr };   // published buffer, nullptr when idle
    std::atomic<Decoded*>    audioHolding { nullptr }; // buffer the audio thread is reading right now (hazard pointer)
    std::vector<std::unique_ptr<Decoded>> owned;    // message thread: every live buffer, including the published one
    juce::uint64             nextId = 1;

    // Audio-thread-owned play state.
    juce::uint64             playingId = 0;
    int                      readPos = 0;
};

} // namespace mu_clid
