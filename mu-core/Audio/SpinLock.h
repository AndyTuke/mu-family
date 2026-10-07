#pragma once

#include <atomic>
#include <thread>

// The family's spin lock on an atomic bool flag (std::atomic<bool> or CopyableSpinLock): the
// audio thread only ever tries (trySpinLock — on contention it keeps last block's data); the
// message thread, whose holds are short, may spin until it gets the lock. Every acquire pairs
// with spinUnlock (release ordering publishes the guarded data).
namespace mu_core
{

// Take the lock if it is free; never waits. Audio-thread safe.
template <typename Flag>
inline bool trySpinLock(Flag& flag) noexcept
{
    bool expected = false;
    return flag.compare_exchange_strong(expected, true, std::memory_order_acquire);
}

// Spin until the lock is taken. Message thread only.
template <typename Flag>
inline void spinLock(Flag& flag) noexcept
{
    while (! trySpinLock(flag)) {}
}

// Try up to `maxTries` times, yielding between tries; false if the lock stayed busy.
template <typename Flag>
inline bool spinLockFor(Flag& flag, int maxTries) noexcept
{
    for (int i = 0; i < maxTries; ++i)
    {
        if (trySpinLock(flag)) return true;
        std::this_thread::yield();
    }
    return false;
}

template <typename Flag>
inline void spinUnlock(Flag& flag) noexcept
{
    flag.store(false, std::memory_order_release);
}

// Holds the lock (spinLock) for its scope.
template <typename Flag>
class ScopedSpinLock
{
public:
    explicit ScopedSpinLock(Flag& f) noexcept : flag(f) { spinLock(flag); }
    ~ScopedSpinLock() { spinUnlock(flag); }
    ScopedSpinLock(const ScopedSpinLock&) = delete;
    ScopedSpinLock& operator=(const ScopedSpinLock&) = delete;
private:
    Flag& flag;
};

} // namespace mu_core
