#pragma once

#include <atomic>

namespace mu_core {

// One rhythm's live play state: written by the audio thread, read by the UI at 30 Hz. Its own
// header so the rhythm circle can show it without knowing which processor owns it.
struct RhythmPlayState
{
    // The play position the rings are drawn from. These values only make sense together (the
    // angle is step + fraction), so they are published and read as one snapshot.
    struct Position
    {
        int   step  = 0;      // current step of the combined pattern
        int   stepC = 0;      // effectiveStep % stepsC - independent of combined-pattern length
        float frac  = 0.0f;   // fractional position within the current step
    };

    // Audio thread, ONE writer only: publishes a whole position at once. A seqlock - the counter is odd
    // while the fields change, so a reader that straddles the write sees a changed counter and retries.
    // Wait-free for the writer: no allocation, no lock, no loop.
    void publishPosition(const Position& p) noexcept
    {
        const unsigned s = positionSeq.load(std::memory_order_relaxed);
        positionSeq.store(s + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);   // the odd counter is visible before any field changes
        posStep .store(p.step,  std::memory_order_relaxed);
        posStepC.store(p.stepC, std::memory_order_relaxed);
        posFrac .store(p.frac,  std::memory_order_relaxed);
        positionSeq.store(s + 2, std::memory_order_release);   // the fields are visible before the even counter
    }

    // UI thread: reads one consistent position without ever blocking the writer. Retries a few times if a
    // publish was in progress, then gives up and returns the last good snapshot (a frame old at worst).
    Position readPosition() const noexcept
    {
        constexpr int kMaxTries = 8;
        Position p;
        for (int i = 0; i < kMaxTries; ++i)
        {
            const unsigned s1 = positionSeq.load(std::memory_order_acquire);
            if (s1 & 1u) continue;   // a publish is in progress
            p.step  = posStep .load(std::memory_order_relaxed);
            p.stepC = posStepC.load(std::memory_order_relaxed);
            p.frac  = posFrac .load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);   // the field reads finish before the counter is re-read
            if (positionSeq.load(std::memory_order_relaxed) == s1)
            {
                lastGood = p;
                return p;
            }
        }
        return lastGood;
    }

    std::atomic<int>  patternLength { 1 };
    std::atomic<int>  stepsA        { 1 }; // individual ring step counts for per-ring rotation
    std::atomic<int>  stepsB        { 1 };
    std::atomic<int>  stepsC        { 1 };
    std::atomic<int>  hitCount      { 0 }; // monotonic counter - audio thread increments per hit, UI tracks lastSeen (avoids a one-shot-flag race between UI readers)

private:
    std::atomic<unsigned> positionSeq { 0 };
    std::atomic<int>      posStep  { 0 };
    std::atomic<int>      posStepC { 0 };
    std::atomic<float>    posFrac  { 0.0f };
    mutable Position      lastGood;   // reader-only (the UI thread); returned when every try straddled a publish
};

} // namespace mu_core
