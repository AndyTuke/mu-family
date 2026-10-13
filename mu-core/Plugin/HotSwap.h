#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cmath>
#include <functional>
#include <utility>
#include <juce_events/juce_events.h>

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

// ── Per-preset swap timing (one family setting, saved with the preset) ─────────────────────────
// WHERE a staged swap lands. The integers are saved data (the `mstr_swap` choice index): append only.
//   OnMasterLoop  the master loop point (falls back to the layer's own loop when no master loop is set)
//   OnLayerLoop   the layer's own loop wrap
//   OnBar         the 4/4 bar line (kBarBeats), for an app with no loop of its own
// "Layer" is the neutral word for a rhythm / voice / pattern.
enum class SwapTiming : int { OnMasterLoop = 0, OnLayerLoop = 1, OnBar = 2 };
inline constexpr const char* kSwapTimingParamId = "mstr_swap";   // the APVTS choice holding it, saved with the preset
inline constexpr int kNumSwapTimings = 3;

// A saved or host-supplied index as a SwapTiming; anything out of range reads as OnMasterLoop.
inline SwapTiming swapTimingFromIndex(int index) noexcept
{
    return (index >= 0 && index < kNumSwapTimings) ? static_cast<SwapTiming>(index) : SwapTiming::OnMasterLoop;
}

// True when a layer's swap waits on the master loop point: the setting is OnMasterLoop AND a master
// loop is actually set. With no master loop (free-running) there is no master wrap to wait for, so the
// swap uses the layer's own loop instead.
inline bool usesMasterLoop(SwapTiming timing, bool hasMasterLoop) noexcept
{
    return timing == SwapTiming::OnMasterLoop && hasMasterLoop;
}

// A per-layer staged swap. OnBar defers to the bar line; otherwise the master loop point when
// usesMasterLoop(), else this layer's own loop wrap (bit `layerIndex` of the mask). `barWrapped` is
// loopWrapped(oldPos, newPos, kBarBeats) for the block.
inline bool layerBoundaryReached(SwapTiming timing, bool hasMasterLoop, int layerIndex,
                                 bool masterLoopWrapped, int layerLoopWrapMask, bool barWrapped) noexcept
{
    if (timing == SwapTiming::OnBar) return barWrapped;
    if (usesMasterLoop(timing, hasMasterLoop)) return masterLoopWrapped;
    // `1 << layerIndex` is only defined inside the mask; the kFullPreset sentinel (-1) must go through
    // fullPresetBoundaryReached, never here.
    return layerIndex >= 0 && layerIndex < 31 && (layerLoopWrapMask & (1 << layerIndex)) != 0;
}

// A full-preset swap spans every layer, so the master loop is the musical boundary whenever one is
// set; free-running, layer 0's loop stands in (waiting for a master wrap that never comes would hang
// the swap). OnBar defers to the bar line instead.
inline bool fullPresetBoundaryReached(SwapTiming timing, bool hasMasterLoop,
                                      bool masterLoopWrapped, int layerLoopWrapMask, bool barWrapped) noexcept
{
    if (timing == SwapTiming::OnBar) return barWrapped;
    return hasMasterLoop ? masterLoopWrapped : ((layerLoopWrapMask & 1) != 0);
}

// The audio thread's "a swap was flagged, commit it" note to the message thread. A plain atomic
// instead of AsyncUpdater::triggerAsyncUpdate(), which allocates a message and so must not be
// called from the audio thread; the message thread polls it on a short timer and drains it.
class CommitMailbox
{
public:
    // Audio thread: lock-free and allocation-free. Release so the flagged word is visible to the drain.
    void request() noexcept { pending.store(true, std::memory_order_release); }

    // Message thread: runs `commit` once if a request is waiting. The flag is cleared BEFORE the
    // commit, so a request that lands during the commit is kept for the next drain.
    template <typename Fn>
    bool drain(Fn&& commit)
    {
        if (! pending.exchange(false, std::memory_order_acq_rel)) return false;
        commit();
        return true;
    }

    bool isPending() const noexcept { return pending.load(std::memory_order_acquire); }

private:
    std::atomic<bool> pending { false };
};

// Staging touches payloads without a lock, which is only sound on the message thread. Headless
// tests with no MessageManager skip the check.
inline void assertMessageThread() noexcept
{
    if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
        jassert (mm->isThisTheMessageThread());
}

// The staging state: N per-slot pending payloads (a channel / voice / layer / lane preset) plus
// one full-preset payload. A full preset replaces every slot, so staging one drops any pending
// per-slot swaps.
//
// Each payload has ONE atomic word: the low two bits are the state (0 empty, 1 ready, 2 flagged)
// and the rest a generation the message thread bumps on every stage / cancel / consume.
//
// Threading contract:
//   - Payloads are only touched on the MESSAGE thread (stage / cancel / consume), which the
//     message loop serialises, so they need no lock.
//   - The audio thread only ever moves a word from ready to flagged, with one compare-exchange
//     against a word it read earlier (a token). Because the message thread changes the generation
//     whenever a payload is replaced or removed, a flag decision made about an old payload can
//     never land on a freshly staged one, however long the audio thread was descheduled between
//     reading the token and flagging.
//   - The message thread sets a word to ready only after the payload is fully written, and to
//     empty only through an exchange / compare-exchange, so it never reads a half-written payload.
template <typename SlotPayload, typename FullPayload, int N>
class Stager
{
public:
    static constexpr int kSlots = N;

    // What the audio thread read about a slot: non-zero only for a ready, not-yet-flagged swap.
    using Token = uint32_t;

    // ── Message thread: staging ──────────────────────────────────────────────
    // Stage a per-slot payload, superseding any swap already pending on that slot. The word goes
    // empty (under a new generation) first so a concurrent flag cannot see a half-written slot.
    void stage(int i, SlotPayload&& payload)
    {
        if (! valid(i)) return;
        put(slots[(size_t) i], std::move(payload));
    }

    void stageFull(FullPayload&& payload)
    {
        for (int i = 0; i < N; ++i) cancel(i);
        put(full, std::move(payload));
    }

    void cancel(int i)
    {
        if (! valid(i)) return;
        clear(slots[(size_t) i]);
    }

    void cancelFull() { clear(full); }

    // Staged and not yet taken: ready or flagged.
    bool hasPending(int i) const noexcept
    {
        return valid(i) && stateOf(slots[(size_t) i].word.load(std::memory_order_acquire)) != kEmpty;
    }
    bool hasFullPending() const noexcept { return stateOf(full.word.load(std::memory_order_acquire)) != kEmpty; }

    // ── Audio thread: boundary detection ─────────────────────────────────────
    // Read the slot BEFORE deciding whether its boundary was reached, then pass the token to
    // flagIfReady: the flag only lands on the very payload the decision was made about.
    Token pendingToken(int i) const noexcept
    {
        return valid(i) ? tokenOf(slots[(size_t) i]) : 0;
    }
    Token fullToken() const noexcept { return tokenOf(full); }

    // Flag a ready swap whose boundary was reached this block. Returns true when newly flagged
    // (the caller then triggers its async commit). The token overloads are the race-free form.
    bool flagIfReady(int i, Token token, bool atBoundary) noexcept
    {
        return valid(i) && atBoundary && flag(slots[(size_t) i], token);
    }
    bool flagFullIfReady(Token token, bool atBoundary) noexcept { return atBoundary && flag(full, token); }

    // Convenience forms that read the token at the call, after `atBoundary` was computed: only for
    // callers where a swap staged during that gap committing at this boundary is acceptable.
    bool flagIfReady(int i, bool atBoundary) noexcept     { return flagIfReady(i, pendingToken(i), atBoundary); }
    bool flagFullIfReady(bool atBoundary) noexcept        { return flagFullIfReady(fullToken(), atBoundary); }

    // ── Message thread: commit ───────────────────────────────────────────────
    // True when slot i's swap is flagged. (A swap cancelled or replaced after the audio thread
    // flagged it is empty or ready again, so it reads false here.)
    bool isFlagged(int i) noexcept
    {
        return valid(i) && stateOf(slots[(size_t) i].word.load(std::memory_order_acquire)) == kFlagged;
    }

    // If slot i is flagged, hand its payload to `apply`, then clear the slot. Returns whether it ran.
    template <typename Fn>
    bool consume(int i, Fn&& apply)
    {
        return valid(i) && take(slots[(size_t) i], kFlagged, std::forward<Fn>(apply));
    }

    // Hand a staged payload to `apply` at once, flagged or not, then clear it. Used when the
    // transport stopped just as the load staged, so no future boundary would ever flag it. The
    // exchange that empties the word also stops the audio thread flagging it from then on.
    template <typename Fn>
    bool consumeNow(int i, Fn&& apply)
    {
        return valid(i) && take(slots[(size_t) i], kAnyStaged, std::forward<Fn>(apply));
    }

    template <typename Fn>
    bool consumeFullNow(Fn&& apply) { return take(full, kAnyStaged, std::forward<Fn>(apply)); }

    template <typename Fn>
    bool consumeFull(Fn&& apply) { return take(full, kFlagged, std::forward<Fn>(apply)); }

private:
    static constexpr uint32_t kEmpty = 0, kReady = 1, kFlagged = 2, kStateMask = 3, kGenStep = 4;
    static constexpr uint32_t kAnyStaged = 0xff;   // `take` selector: ready or flagged

    template <typename P>
    struct Slot
    {
        P                     payload {};
        std::atomic<uint32_t> word { 0 };   // generation << 2 | state
    };

    static constexpr uint32_t stateOf(uint32_t w) noexcept { return w & kStateMask; }

    template <typename P>
    static Token tokenOf(const Slot<P>& s) noexcept
    {
        const uint32_t w = s.word.load(std::memory_order_acquire);
        return stateOf(w) == kReady ? w : 0;
    }

    // Audio thread: ready -> flagged, only if the word is still exactly what the caller read.
    template <typename P>
    static bool flag(Slot<P>& s, Token token) noexcept
    {
        if (token == 0) return false;
        uint32_t expected = token;
        return s.word.compare_exchange_strong(expected, (token & ~kStateMask) | kFlagged,
                                              std::memory_order_acq_rel, std::memory_order_relaxed);
    }

    // Message thread: write the payload under an empty word of a new generation, then publish ready.
    template <typename P, typename V>
    static void put(Slot<P>& s, V&& payload)
    {
        assertMessageThread();
        const uint32_t gen = (s.word.load(std::memory_order_relaxed) & ~kStateMask) + kGenStep;
        s.word.store(gen | kEmpty, std::memory_order_release);   // invalidates any token / flag in flight
        s.payload = std::move(payload);
        s.word.store(gen | kReady, std::memory_order_release);
    }

    // Message thread: drop whatever is staged. The exchange (not a plain store) is a full
    // read-modify-write, so an audio-thread flag either lands before it or fails against it.
    template <typename P>
    static void clear(Slot<P>& s)
    {
        assertMessageThread();
        const uint32_t gen = (s.word.load(std::memory_order_relaxed) & ~kStateMask) + kGenStep;
        s.word.exchange(gen | kEmpty, std::memory_order_acq_rel);
        s.payload = P {};
    }

    // Message thread: take the payload when the word is in the wanted state. The word is emptied
    // (new generation) BEFORE the payload is read, so after it nothing on the audio side can touch
    // the slot, and a later stage() simply starts a new generation. The payload is applied from a
    // local, so a stage() made inside `apply` is safe.
    template <typename P, typename Fn>
    static bool take(Slot<P>& s, uint32_t want, Fn&& apply)
    {
        assertMessageThread();
        uint32_t w = s.word.load(std::memory_order_acquire);
        for (;;)
        {
            const uint32_t st = stateOf(w);
            const bool ok = want == kAnyStaged ? st != kEmpty : st == want;
            if (! ok) return false;
            // A failed exchange means the audio thread just flagged it (ready -> flagged): re-check.
            if (s.word.compare_exchange_weak(w, ((w & ~kStateMask) + kGenStep) | kEmpty,
                                             std::memory_order_acq_rel, std::memory_order_acquire))
                break;
        }
        // Move the payload out first: an apply that stages onto this same slot again must not have
        // its new payload wiped (or the one being applied overwritten) by this call's clean-up.
        P local = std::move(s.payload);
        s.payload = P {};
        apply(local);
        return true;
    }

    static constexpr bool valid(int i) noexcept { return i >= 0 && i < N; }

    std::array<Slot<SlotPayload>, (size_t) N> slots;
    Slot<FullPayload>                         full;
};

// Slot argument the boundary callable receives when asked about the full preset rather than a layer.
inline constexpr int kFullPreset = -1;

// The whole loop-boundary hot-swap for a product whose presets are one payload type for both full
// presets and layer presets (a parsed state tree). The product supplies how to apply each and, per
// block, the loop length that ends each pending swap (its boundary rule); this owns the
// stage-or-apply decision, the commit order and the audio thread's boundary flagging.
template <typename Payload, int N>
class LayerSwapper
{
public:
    using ApplyFull = std::function<void(Payload&)>;
    using ApplySlot = std::function<void(int, Payload&)>;

    // Set once in the owner's constructor (message thread).
    void setAppliers(ApplyFull full, ApplySlot slot) { applyFull = std::move(full); applySlot = std::move(slot); }

    // ── Message thread ───────────────────────────────────────────────────────
    // While the transport runs, stage for the next boundary; while stopped, apply now (dropping
    // anything still staged that would otherwise land on top). "Runs" is the play state the audio
    // thread last resolved. The play state is re-read after staging: if the transport stopped in
    // between, the stop edge may already have passed and nothing would ever flag the payload, so it
    // is taken back and applied here.
    void useFull(Payload&& p)
    {
        if (isRunning())
        {
            stager.stageFull(std::move(p));
            std::atomic_thread_fence(std::memory_order_seq_cst);   // order the staging before the re-read
            if (! isRunning())
                stager.consumeFullNow([this](Payload& staged) { if (applyFull) applyFull(staged); });
            return;
        }
        stager.cancelFull();
        for (int i = 0; i < N; ++i) stager.cancel(i);
        if (applyFull) applyFull(p);
    }

    void useSlot(int i, Payload&& p)
    {
        if (isRunning())
        {
            stager.stage(i, std::move(p));
            std::atomic_thread_fence(std::memory_order_seq_cst);
            if (! isRunning())
            {
                // A full preset still pending predates this slot load: land it first so the async
                // commit cannot overwrite the newer slot afterwards.
                applyPendingFullNow();
                stager.consumeNow(i, [this, i](Payload& staged) { if (applySlot) applySlot(i, staged); });
            }
            return;
        }
        applyPendingFullNow();   // same ordering: the older staged full preset goes first
        stager.cancel(i);
        if (applySlot) applySlot(i, p);
    }

    // The host stopped calling processBlock (prepareToPlay / releaseResources): forget the last
    // resolved play state so the next load applies at once instead of staging for a boundary no
    // audio block will ever flag. Call with the audio thread suspended (it owns wasPlaying).
    void resetPlayState() noexcept
    {
        running.store(false, std::memory_order_relaxed);
        wasPlaying = false;
    }

    // The audio thread's last resolved play state: what a product consults when it must act on the
    // same stage-or-apply decision (e.g. pre-loading assets only for a load that will be staged).
    bool isRunning() const noexcept       { return running.load(std::memory_order_relaxed); }

    // For tests and a product that decides stage-or-apply itself.
    void stageFull(Payload&& p)           { stager.stageFull(std::move(p)); }
    void stageSlot(int i, Payload&& p)    { stager.stage(i, std::move(p)); }

    void cancelFull()                     { stager.cancelFull(); }
    void cancel(int i)                    { stager.cancel(i); }
    bool hasPending(int i) const noexcept { return stager.hasPending(i); }
    bool hasFullPending() const noexcept  { return stager.hasFullPending(); }

    // Commit what reached its boundary: the full preset first (it supersedes the slot swaps), then
    // each flagged slot. Returns true when a full preset was committed.
    bool commit()
    {
        const bool full = stager.consumeFull([this](Payload& p) { if (applyFull) applyFull(p); });
        for (int i = 0; i < N; ++i)
            stager.consume(i, [this, i](Payload& p) { if (applySlot) applySlot(i, p); });
        return full;
    }

    // ── Audio thread ─────────────────────────────────────────────────────────
    // Record the play state and flag staged swaps whose loop point passed in this block (or the
    // transport just stopped). `loopBeatsFor(slot)` gives the loop length in beats that ends the
    // swap pending on `slot` (kFullPreset for the full preset); 0 means no wrap, only the stop
    // edge, and a negative value means the slot is not eligible (it too commits on the stop edge). It is asked only for pending
    // swaps, so it may be a cheap lookup of audio-thread state. Returns true when something was
    // flagged: trigger the async commit.
    template <typename LoopBeatsFor>
    bool flagBoundaries(bool isPlaying, double blockStartBeat, double blockBeats,
                        LoopBeatsFor&& loopBeatsFor) noexcept
    {
        running.store(isPlaying, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);   // pairs with the re-read fence in useFull / useSlot
        const double endBeat = blockStartBeat + (isPlaying ? blockBeats : 0.0);
        auto reached = [&](int slot)
        {
            const double loop = loopBeatsFor(slot);
            // An ineligible slot has no loop to wait for, but once the transport stops nothing is
            // sounding it, so the stop edge still commits it rather than stranding it.
            if (loop < 0.0) return wasPlaying && ! isPlaying;
            return boundaryReached(isPlaying, wasPlaying, blockStartBeat, endBeat, loop);
        };

        // Tokens are read before the boundary rule runs, so a flag can only land on the payload
        // the rule was asked about (see Stager).
        bool flagged = false;
        if (const auto token = stager.fullToken())
            flagged |= stager.flagFullIfReady(token, reached(kFullPreset));
        for (int i = 0; i < N; ++i)
            if (const auto token = stager.pendingToken(i))
                flagged |= stager.flagIfReady(i, token, reached(i));
        wasPlaying = isPlaying;
        return flagged;
    }

    // The common rule: every swap lands on the 4/4 bar line.
    bool flagBoundaries(bool isPlaying, double blockStartBeat, double blockBeats) noexcept
    {
        return flagBoundaries(isPlaying, blockStartBeat, blockBeats, [](int) noexcept { return kBarBeats; });
    }

private:
    void applyPendingFullNow()
    {
        stager.consumeFullNow([this](Payload& staged) { if (applyFull) applyFull(staged); });
    }

    Stager<Payload, Payload, N> stager;
    ApplyFull                   applyFull;
    ApplySlot                   applySlot;
    std::atomic<bool>           running    { false };   // audio thread's play state, read when a load decides
    bool                        wasPlaying = false;     // audio thread only — the play→stop edge
};

} // namespace mu_hotswap
