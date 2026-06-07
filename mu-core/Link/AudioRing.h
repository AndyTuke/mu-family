#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// Lock-free single-producer / single-consumer audio ring (interleaved float frames).
//
// One client writes (the producer, rendering ahead on its own thread); the mu-link
// server reads (the consumer, in its audio callback). With exactly one reader and one
// writer the indices need no locks — only acquire/release ordering on publish. This is
// the JACK ringbuffer contract (jackaudio.org/api/ringbuffer_8h.html).
//
// The ring is split so the *same* read/write algorithm runs both in-process (over a
// std::vector, for unit tests) and cross-process (over a Win32 file-mapping shared by
// the server + a client) with no code change:
//   AudioRingHeader — the indices + geometry; lives at the start of the storage.
//   AudioRingView   — the lock-free algorithm over a (header, data) pair it does NOT own.
//   AudioRing       — owns a std::vector and exposes the same API by delegating to a view.
// For shared memory the server lays an AudioRingHeader at the base of the mapped region
// and the interleaved float storage immediately after it; both processes build a view
// over their own MapViewOfFile pointer and the logic below is byte-for-byte identical.
namespace mu_link
{

// Index + geometry block. Placed at the front of the ring storage so that in shared
// memory a single mapped region is [AudioRingHeader][float data...]. Standard-layout
// with lock-free atomics so it is safe to share across processes by mapping the same
// physical pages — each side sees the other's released writes through the atomics.
struct AudioRingHeader
{
    std::atomic<std::uint32_t> writePos { 0 };   // monotonic frame counts (wrap via % capacity)
    std::atomic<std::uint32_t> readPos  { 0 };
    std::uint32_t              capacity = 0;      // frames
    std::uint32_t              channels = 0;
};

// The lock-free SPSC algorithm over a header + interleaved float storage it does not own.
// Trivially copyable (two pointers) — construct one on demand around any storage.
class AudioRingView
{
public:
    AudioRingView() = default;
    AudioRingView(AudioRingHeader* headerPtr, float* dataPtr) noexcept
        : header(headerPtr), data(dataPtr) {}

    bool valid() const noexcept { return header != nullptr && data != nullptr; }

    // Capacity MUST be a power of two so the monotonic uint32 write/read indices wrap
    // seamlessly across the 2^32 counter boundary (2^32 is an exact multiple of any power
    // of two, so `index % capacity` stays contiguous through the wrap; a non-power-of-two
    // capacity would corrupt the ring after ~2^32 frames). We round UP here so any
    // requested size is safe, and layout() + bytesFor() agree by both rounding identically.
    static std::uint32_t roundedCapacity(int capacityFrames) noexcept
    {
        std::uint32_t c = capacityFrames > 0 ? (std::uint32_t) capacityFrames : 1u;
        std::uint32_t p = 1u;
        while (p < c) p <<= 1;
        return p;
    }

    // Server-side one-time init of a freshly-mapped (or freshly-allocated) region:
    // stamp the geometry and zero the indices. Call once before either side streams.
    static void layout(AudioRingHeader* h, int numChannels, int capacityFrames) noexcept
    {
        h->channels = numChannels > 0 ? (std::uint32_t) numChannels : 1;
        h->capacity = roundedCapacity(capacityFrames);
        h->writePos.store(0, std::memory_order_relaxed);
        h->readPos .store(0, std::memory_order_relaxed);
    }

    // Total bytes a shared region must reserve for [header][capacity*channels floats].
    static std::size_t bytesFor(int numChannels, int capacityFrames) noexcept
    {
        const std::size_t ch  = numChannels > 0 ? (std::size_t) numChannels : 1;
        const std::size_t cap = roundedCapacity(capacityFrames);
        return sizeof(AudioRingHeader) + cap * ch * sizeof(float);
    }

    int numChannels()    const noexcept { return (int) header->channels; }
    int capacityFrames() const noexcept { return (int) header->capacity; }

    // Frames the consumer can read right now / the producer can still write.
    int readAvailable() const noexcept
    {
        const auto w = header->writePos.load(std::memory_order_acquire);
        const auto r = header->readPos .load(std::memory_order_relaxed);
        return (int) (w - r);
    }
    int writeAvailable() const noexcept
    {
        const auto w = header->writePos.load(std::memory_order_relaxed);
        const auto r = header->readPos .load(std::memory_order_acquire);
        return (int) (header->capacity - (std::uint32_t) (w - r));
    }

    // Producer: copy up to `numFrames` interleaved frames in. Returns frames actually
    // written (short when the ring is nearly full — the producer is running ahead, so a
    // short write just means "buffered enough"). `src` holds numFrames*channels floats.
    int writeFrames(const float* src, int numFrames) noexcept
    {
        const std::uint32_t cap = header->capacity;
        const int channels      = (int) header->channels;
        const auto w = header->writePos.load(std::memory_order_relaxed);
        const auto r = header->readPos .load(std::memory_order_acquire);
        const int  freeFrames = (int) (cap - (std::uint32_t) (w - r));
        const int  n = numFrames < freeFrames ? numFrames : freeFrames;

        // Storage is contiguous except across the single wrap point, so copy at most two
        // runs (pre-wrap, post-wrap) with memcpy rather than N modulo-indexed element copies.
        const std::uint32_t start = w % cap;
        const int first = std::min(n, (int) (cap - start));
        std::memcpy(data + (std::size_t) start * channels, src,
                    (std::size_t) first * channels * sizeof(float));
        if (n > first)
            std::memcpy(data, src + (std::size_t) first * channels,
                        (std::size_t) (n - first) * channels * sizeof(float));

        header->writePos.store(w + (std::uint32_t) n, std::memory_order_release);   // publish
        return n;
    }

    // Consumer: copy up to `numFrames` interleaved frames out. Returns frames actually
    // read; a short read is an UNDERRUN — the caller must zero-fill the remainder so the
    // master output never clicks. `dst` has room for numFrames*channels floats.
    int readFrames(float* dst, int numFrames) noexcept
    {
        const std::uint32_t cap = header->capacity;
        const int channels      = (int) header->channels;
        const auto w = header->writePos.load(std::memory_order_acquire);
        const auto r = header->readPos .load(std::memory_order_relaxed);
        const int  avail = (int) (w - r);
        const int  n = numFrames < avail ? numFrames : avail;

        // Mirror of writeFrames: at most two contiguous memcpy runs across the wrap point.
        const std::uint32_t start = r % cap;
        const int first = std::min(n, (int) (cap - start));
        std::memcpy(dst, data + (std::size_t) start * channels,
                    (std::size_t) first * channels * sizeof(float));
        if (n > first)
            std::memcpy(dst + (std::size_t) first * channels, data,
                        (std::size_t) (n - first) * channels * sizeof(float));

        header->readPos.store(r + (std::uint32_t) n, std::memory_order_release);    // release space
        return n;
    }

private:
    AudioRingHeader* header = nullptr;
    float*           data   = nullptr;
};

// In-process owning ring: holds its own header + storage and exposes the SPSC API by
// delegating to an AudioRingView. Used by the unit tests and any single-process path;
// the cross-process server/client build views directly over their mapped regions.
class AudioRing
{
public:
    AudioRing() = default;

    // Allocate storage for `capacityFrames` frames of `numChannels` interleaved floats.
    // Call once before use (message thread); never resized while streaming.
    void prepare(int numChannels, int capacityFrames)
    {
        AudioRingView::layout(&header, numChannels, capacityFrames);
        data.assign((std::size_t) header.capacity * (std::size_t) header.channels, 0.0f);
    }

    int numChannels()    const noexcept { return (int) header.channels; }
    int capacityFrames() const noexcept { return (int) header.capacity; }
    int readAvailable()  const noexcept { return view().readAvailable(); }
    int writeAvailable() const noexcept { return view().writeAvailable(); }

    int writeFrames(const float* src, int numFrames) noexcept { return view().writeFrames(src, numFrames); }
    int readFrames (float* dst,       int numFrames) noexcept { return view().readFrames(dst, numFrames); }

private:
    // A fresh view each call: it is just two pointers, and rebuilding avoids a dangling
    // self-pointer if this AudioRing is moved.
    AudioRingView view() const noexcept
    {
        return AudioRingView(const_cast<AudioRingHeader*>(&header), const_cast<float*>(data.data()));
    }

    AudioRingHeader    header;
    std::vector<float> data;   // capacity * channels interleaved
};

} // namespace mu_link
