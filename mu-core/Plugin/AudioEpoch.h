#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstdint>
#include <thread>

// The audio thread's block counter: the grace period behind every free of something the audio thread
// may have read (a published layer view, a retired layer or tail). docs/mu-tronic/design.md 2.1.2.
//
// The single audio-thread writer bumps the counter on entering and on leaving each block, so it is odd
// while a block runs. A writer on another thread that has just unpublished an object takes a stamp; the
// object may be freed once passed(stamp): either no block was running at the stamp (even), or the counter
// has moved since (that block ended). This is the Dekker pattern (write one variable, then read the
// other), which is why every operation here is seq_cst: in the single total order of seq_cst operations
// the unpublish precedes the stamp, and the stamp precedes the next block's enter(), which precedes that
// block's load, so a later block can only see the new state.
//
// enter / leave are read-modify-writes (not a load and a store), which costs the same as a seq_cst store
// on x64 and stays correct if the host moves processBlock to another thread between blocks. A wrong
// parity (two threads inside at once, outside the JUCE contract) asserts in Debug and, in Release, turns
// reclamation off for good (reclaimDisabled): a leak, never a use-after-free.
namespace mu_core
{
    class AudioEpoch
    {
    public:
        static_assert(std::atomic<uint64_t>::is_always_lock_free, "the epoch is read and written on the audio thread");

        // Audio thread, at the start of a block.
        void enter() noexcept
        {
            const uint64_t r = e.fetch_add(1, std::memory_order_seq_cst);
            if ((r & 1u) != 0) parityFault();
           #if JUCE_DEBUG
            inside.store(juce::Thread::getCurrentThreadId(), std::memory_order_relaxed);
           #endif
        }

        // Audio thread, at the end of the same block.
        void leave() noexcept
        {
           #if JUCE_DEBUG
            inside.store(nullptr, std::memory_order_relaxed);
           #endif
            const uint64_t r = e.fetch_add(1, std::memory_order_seq_cst);
            if ((r & 1u) == 0) parityFault();
        }

        // Any thread: the counter now. Take it AFTER the store that unpublished the object.
        uint64_t stamp() const noexcept { return e.load(std::memory_order_seq_cst); }

        // True once no block that could hold a pointer read before the stamp is still running.
        bool passed(uint64_t s) const noexcept
        {
            return (s & 1u) == 0 || e.load(std::memory_order_seq_cst) != s;
        }

        // Non-audio thread: yields until passed(s) or the timeout; false = timed out (each caller has its
        // own fallback, design 2.1.2 N2). Never waits from inside a block on the same thread.
        bool waitFor(uint64_t s, int timeoutMs) const noexcept
        {
            jassert(! callerIsInsideBlock());   // a writer waiting on its own thread's block would never return
            if (passed(s)) return true;
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + (double) juce::jmax(0, timeoutMs);
            while (! passed(s))
            {
                if (juce::Time::getMillisecondCounterHiRes() >= deadline) return passed(s);
                std::this_thread::yield();
            }
            return true;
        }

        // Sticky: a block was entered or left with the wrong parity. Every reclaimer checks it and frees
        // nothing from then on.
        bool reclaimDisabled() const noexcept { return broken.load(std::memory_order_seq_cst); }

        // Debug builds: whether the calling thread is the one inside a block now (always false in Release).
        bool callerIsInsideBlock() const noexcept
        {
           #if JUCE_DEBUG
            return inside.load(std::memory_order_relaxed) == juce::Thread::getCurrentThreadId();
           #else
            return false;
           #endif
        }

    private:
        void parityFault() noexcept
        {
            broken.store(true, std::memory_order_seq_cst);
            jassertfalse;   // processBlock entered twice at once, or left without entering
        }

        std::atomic<uint64_t> e { 0 };
        std::atomic<bool>     broken { false };
       #if JUCE_DEBUG
        std::atomic<juce::Thread::ThreadID> inside { nullptr };
       #endif
    };

    // Enters the epoch for a scope (the audio thread's block).
    struct ScopedAudioEpoch
    {
        explicit ScopedAudioEpoch(AudioEpoch& ep) noexcept : epoch(ep) { epoch.enter(); }
        ~ScopedAudioEpoch() noexcept { epoch.leave(); }
        ScopedAudioEpoch(const ScopedAudioEpoch&) = delete;
        ScopedAudioEpoch& operator=(const ScopedAudioEpoch&) = delete;
        AudioEpoch& epoch;
    };
}
