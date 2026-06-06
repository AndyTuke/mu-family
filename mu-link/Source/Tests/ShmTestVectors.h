#pragma once

#include "Link/MuLinkSharedMemory.h"
#include <cstdint>

// Shared fixtures for the cross-process shared-memory test. The parent (acting as the
// mu-link server) and the spawned child (acting as a standalone client) both include
// this so they agree on the exact transport values and ring payload exchanged. Keeping
// them in one header is the single source of truth for the round-trip vectors.
namespace mu_link_test
{

constexpr std::uint64_t kSamplePos   = 1234567;
constexpr double        kTempo       = 132.0;
constexpr std::uint32_t kSampleRate  = 48000;
constexpr std::uint32_t kBlockSize   = 256;
constexpr std::uint32_t kGeneration  = 7;
constexpr int           kRingFrames  = 100;
constexpr int           kRingChans   = 2;
constexpr const char*   kClientName  = "shm-child";

// Deterministic, position-dependent sample so a reorder or drop is detectable.
inline float ringSample(int frame, int ch) noexcept
{
    return (float) (frame * 10 + ch) + 0.25f;
}

#ifdef _WIN32

// Child-process body: attach as a client, verify the transport the parent published,
// claim a slot, and push the known ring payload as a producer. Returns 0 on success;
// a non-zero code identifies which step failed (read by the parent as the exit code).
inline int runShmChild()
{
    mu_link::MuLinkClientMemory client;
    if (! client.open())                                   // mu-link not running / version mismatch
        return 2;

    const auto& t = client.transport();
    if (t.samplePos .load(std::memory_order_acquire) != kSamplePos)  return 3;
    if (t.sampleRate.load(std::memory_order_acquire) != kSampleRate) return 3;
    if (t.blockSize .load(std::memory_order_acquire) != kBlockSize)  return 3;
    if (t.playing   .load(std::memory_order_acquire) != 1u)          return 3;
    if (t.generation.load(std::memory_order_acquire) != kGeneration) return 3;
    if (t.tempoBpm  .load(std::memory_order_acquire) != kTempo)      return 3;

    const int slot = client.claimSlot(kClientName, kRingChans);
    if (slot < 0)
        return 4;

    float frames[kRingFrames * kRingChans];
    for (int i = 0; i < kRingFrames; ++i)
        for (int c = 0; c < kRingChans; ++c)
            frames[i * kRingChans + c] = ringSample(i, c);

    if (client.ring().writeFrames(frames, kRingFrames) != kRingFrames)
        return 5;

    // Leave the slot claimed + frames in the ring; the parent reads them back after we
    // exit. Shared pages persist while the parent still holds the mapping handles.
    return 0;
}

#endif // _WIN32

} // namespace mu_link_test
