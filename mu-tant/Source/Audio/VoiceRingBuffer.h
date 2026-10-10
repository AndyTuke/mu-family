#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>

namespace mu_tant
{

// Lock-free audio-to-UI ring buffer — written by the audio thread in renderVoice()
// after the insert, read by VoiceSpectrumGlyph at 30 Hz to drive the sidebar animation.
// kSize must be a power of 2 (enables fast bit-mask indexing).
struct VoiceRingBuffer
{
    static constexpr int kSize = 1024;   // ~21 ms at 48 kHz

    // Audio thread: mono-mix buf and append n frames.
    void write(const juce::AudioBuffer<float>& buf, int n) noexcept
    {
        const int nCh  = buf.getNumChannels();
        const float sc = nCh > 0 ? 1.0f / (float) nCh : 0.0f;
        // Unsigned, so the running count wraps by definition rather than overflowing.
        unsigned head = (unsigned) writeHead.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            float s = 0.0f;
            for (int c = 0; c < nCh; ++c)
                s += buf.getSample(c, i);
            data[(size_t)(head & (kSize - 1))] = s * sc;
            ++head;
        }
        writeHead.store((int) head, std::memory_order_release);
    }

    // UI thread: copy the most-recent n samples into out[].
    void read(float* out, int n) const noexcept
    {
        const unsigned head  = (unsigned) writeHead.load(std::memory_order_acquire);
        const unsigned start = head - (unsigned) n;
        for (int i = 0; i < n; ++i)
            out[i] = data[(size_t)((start + (unsigned) i) & (kSize - 1))];
    }

    std::array<float, kSize> data {};
    std::atomic<int>         writeHead { 0 };
};

} // namespace mu_tant
