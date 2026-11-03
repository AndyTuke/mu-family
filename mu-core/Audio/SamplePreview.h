#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>
#include <memory>
#include <vector>

namespace mu_core {

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
    // Whether the audition repeats from the start when it reaches its end (any thread).
    void setLooping(bool shouldLoop) noexcept { looping.store(shouldLoop, std::memory_order_relaxed); }
    // Message thread: plays decoded audio (any channel count, at `audioRate`) the same way, converted to the prepared rate.
    void startBuffer(const juce::AudioBuffer<float>& audio, double audioRate);

    // The part of the published audio that plays, as fractions of its length (any thread, takes effect at once, also mid-play): the
    // start is where a new play begins and where a repeat returns to, the end is where it stops or repeats. Default 0 to 1.
    void setRegion(float start, float end) noexcept { regionStart.store(start, std::memory_order_relaxed); regionEnd.store(end, std::memory_order_relaxed); }
    // Where the audition is, as a fraction of the published audio (0..1), or a negative value when nothing sounds (any thread).
    float getPosition() const noexcept { return position.load(std::memory_order_relaxed); }

    // Audio thread: mix preview output into masterBus if currently playing.
    void mixInto(juce::AudioBuffer<float>& masterBus, int numSamples);

    static constexpr double kMaxSeconds = 30.0;

private:
    struct Decoded
    {
        juce::AudioBuffer<float> audio;   // 2 channels at the prepared sample rate
        juce::uint64             id = 0;  // unique per decode, so a recycled address is never mistaken for the same preview
    };

    // Message thread: converts a stereo source (with zero padding after srcLen) to the prepared rate and publishes it.
    void publish(juce::AudioBuffer<float>& src, int srcLen, double srcRate, double rate);

    // Message thread: free retired buffers the audio thread is no longer reading.
    void collectRetired();

    juce::CriticalSection    messageLock;   // serialises start/stop/prepare/retire; the audio thread never takes it
    juce::AudioFormatManager formatManager;
    double                   sampleRate = 0.0;
    int                      chunkMax = 0;

    std::atomic<Decoded*>    current { nullptr };   // published buffer, nullptr when idle
    std::atomic<Decoded*>    audioHolding { nullptr }; // buffer the audio thread is reading right now (hazard pointer)
    std::vector<std::unique_ptr<Decoded>> owned;    // message thread: every live buffer, including the published one
    juce::uint64             nextId = 1;
    std::atomic<bool>        looping { false };
    std::atomic<float>       regionStart { 0.0f }, regionEnd { 1.0f };
    std::atomic<float>       position { -1.0f };

    // Audio-thread-owned play state.
    juce::uint64             playingId = 0;
    int                      readPos = 0;
    bool                     finished = false;   // a play that has reached the region's end and does not repeat
};

} // namespace mu_core

