#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

// Lock-free single-producer / single-consumer audio ring (interleaved float frames).
//
// One client writes (the producer, rendering ahead on its own thread); the mu-link
// server reads (the consumer, in its audio callback). With exactly one reader and one
// writer the indices need no locks — only acquire/release ordering on publish. This is
// the JACK ringbuffer contract (jackaudio.org/api/ringbuffer_8h.html).
//
// The storage + indices are kept as plain members here so the algorithm is easy to
// test in-process. Lifting it cross-process is purely relocation: place `head` (the two
// atomics) and the sample storage in a Win32 file-mapping region shared by both
// processes — the read/write logic below is unchanged.
namespace mu_link
{

class AudioRing
{
public:
    AudioRing() = default;

    // Allocate storage for `capacityFrames` frames of `numChannels` interleaved floats.
    // Call once before use (message thread); never resized while streaming.
    void prepare(int numChannels, int capacityFrames)
    {
        channels = numChannels > 0 ? numChannels : 1;
        capacity = capacityFrames > 0 ? (std::uint32_t) capacityFrames : 1;
        data.assign((std::size_t) capacity * (std::size_t) channels, 0.0f);
        writePos.store(0, std::memory_order_relaxed);
        readPos .store(0, std::memory_order_relaxed);
    }

    int numChannels()    const noexcept { return channels; }
    int capacityFrames() const noexcept { return (int) capacity; }

    // Frames the consumer can read right now / the producer can still write.
    int readAvailable() const noexcept
    {
        const auto w = writePos.load(std::memory_order_acquire);
        const auto r = readPos .load(std::memory_order_relaxed);
        return (int) (w - r);
    }
    int writeAvailable() const noexcept
    {
        const auto w = writePos.load(std::memory_order_relaxed);
        const auto r = readPos .load(std::memory_order_acquire);
        return (int) (capacity - (std::uint32_t) (w - r));
    }

    // Producer: copy up to `numFrames` interleaved frames in. Returns frames actually
    // written (short when the ring is nearly full — the producer is running ahead, so a
    // short write just means "buffered enough"). `src` holds numFrames*channels floats.
    int writeFrames(const float* src, int numFrames) noexcept
    {
        const auto w = writePos.load(std::memory_order_relaxed);
        const auto r = readPos .load(std::memory_order_acquire);
        const int  freeFrames = (int) (capacity - (std::uint32_t) (w - r));
        const int  n = numFrames < freeFrames ? numFrames : freeFrames;

        for (int i = 0; i < n; ++i)
        {
            const std::size_t slot = (std::size_t) ((w + (std::uint32_t) i) % capacity) * (std::size_t) channels;
            for (int c = 0; c < channels; ++c)
                data[slot + (std::size_t) c] = src[(std::size_t) i * (std::size_t) channels + (std::size_t) c];
        }
        writePos.store(w + (std::uint32_t) n, std::memory_order_release);   // publish
        return n;
    }

    // Consumer: copy up to `numFrames` interleaved frames out. Returns frames actually
    // read; a short read is an UNDERRUN — the caller must zero-fill the remainder so the
    // master output never clicks. `dst` has room for numFrames*channels floats.
    int readFrames(float* dst, int numFrames) noexcept
    {
        const auto w = writePos.load(std::memory_order_acquire);
        const auto r = readPos .load(std::memory_order_relaxed);
        const int  avail = (int) (w - r);
        const int  n = numFrames < avail ? numFrames : avail;

        for (int i = 0; i < n; ++i)
        {
            const std::size_t slot = (std::size_t) ((r + (std::uint32_t) i) % capacity) * (std::size_t) channels;
            for (int c = 0; c < channels; ++c)
                dst[(std::size_t) i * (std::size_t) channels + (std::size_t) c] = data[slot + (std::size_t) c];
        }
        readPos.store(r + (std::uint32_t) n, std::memory_order_release);    // release space
        return n;
    }

private:
    int                        channels = 1;
    std::uint32_t              capacity = 1;
    std::vector<float>         data;             // capacity * channels interleaved
    std::atomic<std::uint32_t> writePos { 0 };   // monotonic frame counts (wrap via % capacity)
    std::atomic<std::uint32_t> readPos  { 0 };
};

} // namespace mu_link
