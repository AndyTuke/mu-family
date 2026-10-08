#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <utility>

// Loop-boundary preset hot-swap, shared by every product. While the transport plays, a loaded
// preset is staged (parsed and pre-loaded off the audio thread) and committed when its loop point
// is reached, so the switch is musically seamless; products keep their own boundary rule (which
// loop) and their own apply.
namespace mu_hotswap
{

// One 4/4 bar in beats — the swap point for an app with no loop of its own (mu-Toni's arp,
// mu-On's 16-step pattern).
inline constexpr double kBarBeats = 4.0;

// True when a loop of `loopBeats` wrapped between `oldPos` and `newPos` (beats; `newPos` is the
// raw advanced position, before any transport ceiling wrap, so the loop-index test stays valid
// for lengths that don't divide the ceiling). A wrap = the integer loop index advanced.
inline bool loopWrapped(double oldPos, double newPos, double loopBeats) noexcept
{
    if (loopBeats <= 0.0) return false;
    return std::floor(newPos / loopBeats) != std::floor(oldPos / loopBeats);
}

// Whether a staged swap should commit this block: playing → when the reference loop wraps;
// the playing→stopped edge → at once (nothing is sounding a pattern any more); stopped with no
// edge → never (a load while stopped is applied at once, so nothing is pending).
inline bool boundaryReached(bool playing, bool wasPlaying,
                            double oldPos, double newPos, double loopBeats) noexcept
{
    if (wasPlaying && ! playing) return true;
    if (playing)                 return loopWrapped(oldPos, newPos, loopBeats);
    return false;
}

// The staging state: N per-slot pending payloads (a channel / voice / layer / lane preset) plus
// one full-preset payload. A full preset replaces every slot, so staging one drops any pending
// per-slot swaps.
//
// Threading contract:
//   - Payloads are only touched on the MESSAGE thread (stage / cancel / consume), which the
//     message loop serialises, so they need no lock.
//   - The audio thread touches ONLY the flags: flagIfReady() reads `isReady` (acquire) and sets
//     `boundaryReached` (release); consume() then sees both on the message thread.
template <typename SlotPayload, typename FullPayload, int N>
class Stager
{
public:
    static constexpr int kSlots = N;

    // ── Message thread: staging ──────────────────────────────────────────────
    // Stage a per-slot payload, superseding any swap already pending on that slot. `isReady`
    // drops first so a concurrent flagIfReady() can't observe a half-written slot.
    void stage(int i, SlotPayload&& payload)
    {
        if (! valid(i)) return;
        auto& s = slots[(size_t) i];
        s.isReady.store(false, std::memory_order_release);
        s.boundaryReached.store(false, std::memory_order_relaxed);
        s.payload = std::move(payload);
        s.isReady.store(true, std::memory_order_release);
    }

    void stageFull(FullPayload&& payload)
    {
        for (int i = 0; i < N; ++i) cancel(i);
        full.isReady.store(false, std::memory_order_release);
        full.boundaryReached.store(false, std::memory_order_relaxed);
        full.payload = std::move(payload);
        full.isReady.store(true, std::memory_order_release);
    }

    void cancel(int i)
    {
        if (! valid(i)) return;
        auto& s = slots[(size_t) i];
        s.isReady.store(false, std::memory_order_release);
        s.boundaryReached.store(false, std::memory_order_relaxed);
        s.payload = SlotPayload {};
    }

    void cancelFull()
    {
        full.isReady.store(false, std::memory_order_release);
        full.boundaryReached.store(false, std::memory_order_relaxed);
        full.payload = FullPayload {};
    }

    bool hasPending(int i) const noexcept
    {
        return valid(i) && slots[(size_t) i].isReady.load(std::memory_order_acquire);
    }
    bool hasFullPending() const noexcept { return full.isReady.load(std::memory_order_acquire); }

    // ── Audio thread: boundary detection ─────────────────────────────────────
    // Flag a ready, not-yet-flagged swap whose boundary was reached this block. Returns true when
    // newly flagged (the caller then triggers its async commit).
    bool flagIfReady(int i, bool atBoundary) noexcept
    {
        return valid(i) && atBoundary && flag(slots[(size_t) i]);
    }
    bool flagFullIfReady(bool atBoundary) noexcept { return atBoundary && flag(full); }

    // ── Message thread: commit ───────────────────────────────────────────────
    // True when slot i's swap is flagged and still staged. A swap cancelled after the audio thread
    // flagged it has its stale flag cleared here.
    bool isFlagged(int i) noexcept
    {
        if (! valid(i)) return false;
        auto& s = slots[(size_t) i];
        if (! s.boundaryReached.load(std::memory_order_acquire)) return false;
        if (! s.isReady.load(std::memory_order_acquire))
        {
            s.boundaryReached.store(false, std::memory_order_relaxed);
            return false;
        }
        return true;
    }

    // If slot i is flagged, hand its payload to `apply`, then clear the slot. Returns whether it ran.
    template <typename Fn>
    bool consume(int i, Fn&& apply)
    {
        if (! isFlagged(i)) return false;
        auto& s = slots[(size_t) i];
        apply(s.payload);
        s.payload = SlotPayload {};
        s.boundaryReached.store(false, std::memory_order_relaxed);
        s.isReady.store(false, std::memory_order_release);
        return true;
    }

    template <typename Fn>
    bool consumeFull(Fn&& apply)
    {
        if (! (full.isReady.load(std::memory_order_acquire)
               && full.boundaryReached.load(std::memory_order_acquire)))
            return false;
        apply(full.payload);
        full.payload = FullPayload {};
        full.boundaryReached.store(false, std::memory_order_relaxed);
        full.isReady.store(false, std::memory_order_release);
        return true;
    }

private:
    template <typename P>
    struct Slot
    {
        P                 payload {};
        std::atomic<bool> isReady         { false };
        std::atomic<bool> boundaryReached { false };
    };

    template <typename P>
    static bool flag(Slot<P>& s) noexcept
    {
        if (! s.isReady.load(std::memory_order_acquire) || s.boundaryReached.load(std::memory_order_relaxed))
            return false;
        s.boundaryReached.store(true, std::memory_order_release);
        return true;
    }

    static constexpr bool valid(int i) noexcept { return i >= 0 && i < N; }

    std::array<Slot<SlotPayload>, (size_t) N> slots;
    Slot<FullPayload>                         full;
};

// The whole bar-line hot-swap for a product whose presets are one payload type for both full
// presets and slot presets (a parsed state tree) and whose swap point is a fixed loop length
// (mu-Toni's arp bar, mu-On's pattern wrap). The product supplies how to apply each; this owns
// the stage-or-apply decision, the commit order and the audio thread's boundary flagging.
template <typename Payload, int N>
class BarLineSwapper
{
public:
    using ApplyFull = std::function<void(Payload&)>;
    using ApplySlot = std::function<void(int, Payload&)>;

    // Set once in the owner's constructor (message thread).
    void setAppliers(ApplyFull full, ApplySlot slot) { applyFull = std::move(full); applySlot = std::move(slot); }

    // ── Message thread ───────────────────────────────────────────────────────
    // While the transport runs, stage for the next boundary; while stopped, apply now (dropping
    // anything still staged that would otherwise land on top).
    void useFull(Payload&& p)
    {
        if (running.load(std::memory_order_relaxed)) { stager.stageFull(std::move(p)); return; }
        stager.cancelFull();
        for (int i = 0; i < N; ++i) stager.cancel(i);
        applyFull(p);
    }

    void useSlot(int i, Payload&& p)
    {
        if (running.load(std::memory_order_relaxed)) { stager.stage(i, std::move(p)); return; }
        stager.cancel(i);
        applySlot(i, p);
    }

    void cancel(int i)                    { stager.cancel(i); }
    bool hasPending(int i) const noexcept { return stager.hasPending(i); }
    bool hasFullPending() const noexcept  { return stager.hasFullPending(); }

    // Commit what reached its boundary: the full preset first (it supersedes the slot swaps), then
    // each flagged slot. Returns true when a full preset was committed.
    bool commit()
    {
        const bool full = stager.consumeFull([this](Payload& p) { applyFull(p); });
        for (int i = 0; i < N; ++i)
            stager.consume(i, [this, i](Payload& p) { applySlot(i, p); });
        return full;
    }

    // ── Audio thread ─────────────────────────────────────────────────────────
    // Record the play state and flag staged swaps whose loop point passed in this block (or the
    // transport just stopped). Returns true when something was flagged: trigger the async commit.
    bool flagBoundaries(bool isPlaying, double blockStartBeat, double blockBeats,
                        double loopBeats = kBarBeats) noexcept
    {
        running.store(isPlaying, std::memory_order_relaxed);
        const bool atLoop = boundaryReached(isPlaying, wasPlaying, blockStartBeat,
                                            blockStartBeat + (isPlaying ? blockBeats : 0.0), loopBeats);
        wasPlaying = isPlaying;
        bool flagged = stager.flagFullIfReady(atLoop);
        for (int i = 0; i < N; ++i) flagged |= stager.flagIfReady(i, atLoop);
        return flagged;
    }

private:
    Stager<Payload, Payload, N> stager;
    ApplyFull                   applyFull;
    ApplySlot                   applySlot;
    std::atomic<bool>           running    { false };   // audio thread's play state, read when a load decides
    bool                        wasPlaying = false;     // audio thread only — the play→stop edge
};

} // namespace mu_hotswap
