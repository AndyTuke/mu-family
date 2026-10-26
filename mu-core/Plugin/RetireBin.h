#pragma once

#include "Sequencer/RetireTail.h"
#include "Plugin/AudioEpoch.h"
#include "MuLimits.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

// The shared retire bin: where a layer (or a product's engine tail) keeps sounding after it left its slot,
// until it is silent, its fade is spent or its hard lifetime ends. docs/mu-tronic/design.md 2.1.3 (C2).
//
// Shape: per-slot rows of kRetireDepth entries (a tail renders into its own slot's strip, and a structural
// edit moves owned tails with their slot), owned by ProcessorBase. Each entry runs one four-state machine:
//
//   From      To        Thread   Operation                                    When
//   Empty     Parked    message  fill the payload, then state.store(Parked)   park, parkInPlace
//   Parked    Draining  audio    CAS; then onRetireStart                      first rendered block that reached the activation serial
//   Parked    Done      message  CAS                                          cut before the audio thread started it
//   Draining  Done      audio    CAS                                          silent, fade spent or lifetime over
//   Draining  Done      message  CAS                                          eviction, a cut, a structural edit, cutAll
//   Done      Empty     message  stamp at first sight; free once passed       service
//
// Every operation on `state` is seq_cst; the payload is plain data published by the store that leaves
// Empty. The audio thread never touches an Empty or Done entry and never writes `tail` or `owned`; the
// message thread never writes the payload of a non-Empty entry. A failed audio CAS means the message thread
// cut the entry, and the audio thread skips it. Frees wait for the AudioEpoch grace period stamped after
// Done was observed, so the block that may still have been inside renderRetiring has ended.
//
// Activation serial (same-block hand-over): an entry renders only in blocks whose layout serial has reached
// its `activation` (wrap-safe compare), and it is parked before the store that publishes that serial, so the
// block that still sees the old layout renders the old layer live and skips the entry, and the first block
// that sees the new layout renders it as a tail. No block has both and none has neither.
//
// Drain and free semantics (defined here for every product): an entry is Done when (a) renderRetiring returns
// false (at once; a Natural tail with closeWithDeclick after the same 10 ms declick as (c)), (b) a Fade policy's ramp is spent, (c) its hard lifetime (maxSeconds, clamped to 30 s) is over, with
// a 10 ms linear declick in its last frames, or (d) the message thread cuts it. A tail that never reports
// silence (an engine with nothing loaded that never says drained) therefore still ends at (c). Done entries
// are freed only by service(), on the message thread, outside layersLock, once the grace passed: it runs at
// the top of each deferred-work pass (which the audio thread requests when an entry goes Done), on the
// 100 Hz poll while hasPendingWork(), on the headless flush, and at prepare / release / destruction.
//
// Locks: every message-side operation takes binMutex (never the audio thread, never layersLock). Lock order
// layoutWriteLock, layersLock, binMutex; nobody takes layersLock while holding binMutex. binMutex is held only
// for bookkeeping: nothing is destroyed and nothing waits while it is held. A function collects what it frees
// in a stack FreeList that is destroyed after the mutex is released; a caller that holds layersLock
// (audioExcluded) hands its frees to the reclaim list instead, for a later service pass outside that lock; an
// eviction that must wait for a grace period releases the mutex for the wait and re-validates afterwards.
namespace mu_core
{
    class RetireBin
    {
    public:
        enum State : uint8_t { Empty = 0, Parked, Draining, Done };
        enum class CountPolicy : uint8_t { RenderPastCount, CutAtCount };   // per product, set once at construction
        enum class ParkResult : uint8_t
        {
            Parked,          // the entry will render from its activation serial on
            NotParked,       // the tail's policy is None: it is freed after the caller's store and a grace period
            AlreadyActive,   // parkInPlace on a row that already has an active in-place entry: no-op
            Dropped,         // the row was full and its eviction failed: the tail is freed after the caller's store and a grace
            Invalid          // slot out of range or no tail
        };

        static constexpr int kRows = mu_limits::kMaxLayers;
        static constexpr int kDepth = mu_limits::kRetireDepth;
        static constexpr int kEntries = kRows * kDepth;
        // The reclaim list holds twice the bin's entries: everything evicted under layersLock in one pass plus
        // every NotParked or dropped tail can wait there for a service pass.
        static constexpr int kReclaimCapacity = 2 * kEntries;

        // `layoutWord`: the processor's packed count | serial word (LayoutWord), read to know when the store that
        // unpublishes a parked layer has landed (an outgoing tail the bin did not take is freed only after it).
        RetireBin(const AudioEpoch& e, const std::atomic<uint64_t>& layoutWord) noexcept : epoch(e), layout(layoutWord) {}
        ~RetireBin() = default;   // owners call cutAll + service from prepareForDestruction first
        RetireBin(const RetireBin&) = delete;
        RetireBin& operator=(const RetireBin&) = delete;

        // ─── Configuration (message thread, before processing) ───────────────────────────────────────
        void        setCountPolicy(CountPolicy p) noexcept { policy = p; }
        CountPolicy countPolicy() const noexcept           { return policy; }
        // Tails sounding at once in this instance; when reached, a park evicts the oldest active entry.
        void setGlobalBudget(int maxLive) { BinLock g(binMutex); budget = juce::jlimit(1, kEntries, maxLive); }
        // How long an eviction outside layersLock waits (with binMutex released) for the grace period before
        // dropping the incoming tail. The caller of such a park usually holds layoutWriteLock (its park and its count
        // store share one hold), so a host thread's setStateInformation can be held up by up to this long.
        void setEvictTimeoutMs(int ms) { BinLock g(binMutex); evictTimeoutMs = juce::jmax(0, ms); }

        // prepareToPlay: the rate park converts seconds with, and the declick scratch (the host is not
        // processing). Allocates on the calling (message or host) thread.
        void setPrepared(double sampleRate, int blockSize)
        {
            BinLock g(binMutex);
            preparedRate = sampleRate;
            declick.setSize(2, juce::jmax(1, blockSize), false, true, false);
            declickFrames = juce::jmax(1, (int) (sampleRate * (mu_limits::kRetireDeclickMs / 1000.0)));
        }

        // ─── Message side (every one takes binMutex) ─────────────────────────────────────────────────
        // An owned tail (a whole layer, an engine). `audioExcluded`: the caller holds layersLock in TryLock
        // mode, so the audio thread cannot be inside the bin; an eviction then empties the entry at once, without
        // a wait, and every free goes to the reclaim list (never a free under the caller's layersLock).
        //
        // The outgoing tail may still be LIVE when park is called: a park outside layersLock comes before the count
        // store that unpublishes it (its activation is that store's serial). So a tail the bin does not take
        // (NotParked, Dropped) is never freed inside park: it waits in the reclaim list until the published serial
        // has reached `activation`, then for a grace period stamped after that, and only then is it freed.
        ParkResult park(int slot, std::unique_ptr<RetireTail> owned, uint32_t activation, bool audioExcluded)
        {
            if (slot < 0 || slot >= kRows || owned == nullptr) return ParkResult::Invalid;
            FreeList frees;                   // destroyed after the lock below is released
            BinLock g(binMutex);
            if (owned->retirePolicy().kind == RetirePolicy::Kind::None)
            {
                retireLater(std::move(owned), activation, frees);   // still live until the caller's store: after it, plus a grace
                return ParkResult::NotParked;
            }
            Entry* e = placeLocked(slot, audioExcluded, g, frees);
            if (e == nullptr)
            {
                retireLater(std::move(owned), activation, frees);   // the outgoing layer: same rule as NotParked
                return ParkResult::Dropped;
            }
            RetireTail& t = *owned;
            e->owned = std::move(owned);
            fillLocked(*e, t, activation, false);
            return ParkResult::Parked;
        }

        // A borrowed spare that stays in its slot (mu-Tant keeps the dropped slot's layer and engine). A row
        // holds at most one active in-place entry: a second park is a no-op and the running fade continues.
        ParkResult parkInPlace(int slot, RetireTail& spare, uint32_t activation, bool audioExcluded)
        {
            if (slot < 0 || slot >= kRows) return ParkResult::Invalid;
            FreeList frees;
            BinLock g(binMutex);
            if (hasActiveInPlaceLocked(slot)) return ParkResult::AlreadyActive;
            if (spare.retirePolicy().kind == RetirePolicy::Kind::None) return ParkResult::NotParked;
            Entry* e = placeLocked(slot, audioExcluded, g, frees);
            if (e == nullptr) return ParkResult::Dropped;
            if (hasActiveInPlaceLocked(slot)) return ParkResult::AlreadyActive;   // another park won during an eviction wait
            fillLocked(*e, spare, activation, true);
            return ParkResult::Parked;
        }

        // Every active entry of the slot to Done: a remove, a regrow into an in-place slot (AFTER the count
        // store), before a spare is destroyed.
        void cutSlot(int slot)
        {
            if (slot < 0 || slot >= kRows) return;
            BinLock g(binMutex);
            for (auto& e : rows[(size_t) slot]) cutLocked(e);
        }

        // CutAtCount products: a count-reducing store cuts the rows it leaves (after the store).
        void cutAtOrAbove(int count)
        {
            BinLock g(binMutex);
            for (int s = juce::jmax(0, count); s < kRows; ++s)
                for (auto& e : rows[(size_t) s]) cutLocked(e);
        }

        // Structural edits, with the audio thread excluded (layersLock in TryLock mode, quiesce in Published):
        // owned tails follow their slot; in-place entries are CUT, never moved (their spare does not move).
        void swapRows(int a, int b)
        {
            if (a == b || a < 0 || b < 0 || a >= kRows || b >= kRows) return;
            BinLock g(binMutex);
            for (int s : { a, b })
                for (auto& e : rows[(size_t) s])
                    if (e.inPlace != 0) cutLocked(e);
            for (int k = 0; k < kDepth; ++k)
                swapEntries(rows[(size_t) a][(size_t) k], rows[(size_t) b][(size_t) k]);
        }

        // A remove at `index` with `liveCount` live layers before it: the removed row is cut, the rows above it
        // up to the old top move down one with their layers, and the cut row lands in the vacated top slot,
        // where service frees it. Rows at or above liveCount (spares past the count) do not move.
        void removeRow(int index, int liveCount)
        {
            if (index < 0 || index >= kRows) return;
            BinLock g(binMutex);
            for (auto& e : rows[(size_t) index]) cutLocked(e);
            const int top = juce::jmin(kRows, liveCount) - 1;
            for (int s = index + 1; s <= top; ++s)
                for (auto& e : rows[(size_t) s])
                    if (e.inPlace != 0) cutLocked(e);
            for (int s = index; s < top; ++s)
                for (int k = 0; k < kDepth; ++k)
                    swapEntries(rows[(size_t) s][(size_t) k], rows[(size_t) (s + 1)][(size_t) k]);
        }

        // prepareToPlay, releaseResources, prepareForDestruction: everything to Done (then call service).
        void cutAll()
        {
            BinLock g(binMutex);
            for (auto& row : rows)
                for (auto& e : row) cutLocked(e);
        }

        // Frees every Done entry (and reclaim-list tail) whose grace has passed. Never waits; an entry whose
        // grace has not passed waits for the next pass. Message thread, outside layersLock. The destructors run
        // after binMutex is released. Returns the count freed.
        // `destroying`: the owner is going away and nothing will process again, so a tail still waiting for its
        // unpublishing store is freed too (prepareForDestruction).
        int service(bool destroying = false)
        {
            FreeList frees;
            BinLock g(binMutex);
            // Cleared before the scan: an entry the audio thread marks Done during it raises the flag again.
            pending.store(false, std::memory_order_seq_cst);
            const bool canFree = ! epoch.reclaimDisabled();
            bool left = false;
            int released = 0;
            for (auto& row : rows)
                for (auto& e : row)
                {
                    if (e.state.load(std::memory_order_seq_cst) != Done) continue;
                    stampDoneLocked(e);
                    if (canFree && epoch.passed(e.doneStamp)) { frees.add(releaseLocked(e)); ++released; }
                    else left = true;
                }
            for (int i = 0; i < reclaimCount; )
            {
                auto& r = reclaim[(size_t) i];
                // A tail that was live waits for the store that unpublished it, THEN for a grace stamped after it.
                const bool unpublished = destroying || ! r.hasNotBefore
                                      || (int32_t) (publishedSerial() - r.notBefore) >= 0;
                if (r.needsGrace && ! r.stamped && unpublished) { r.stamped = true; r.stamp = epoch.stamp(); }
                if (destroying || (unpublished && (! r.needsGrace || (canFree && epoch.passed(r.stamp)))))
                {
                    frees.add(std::move(r.owned));
                    if (i != reclaimCount - 1) reclaim[(size_t) i] = std::move(reclaim[(size_t) (reclaimCount - 1)]);
                    --reclaimCount;
                    ++released;
                }
                else { left = true; ++i; }
            }
            if (left) pending.store(true, std::memory_order_seq_cst);
            return released;
        }

        // Any thread: something is Done or waiting in the reclaim list, so a service pass has work.
        bool hasPendingWork() const noexcept { return pending.load(std::memory_order_seq_cst); }

        // ─── Audio thread, inside a LayerBlock that succeeded (a failed block calls nothing here) ────
        // The highest slot with an entry that renders under `viewSerial`, or -1 (RenderPastCount's mixCount).
        int highestActiveSlot(uint32_t viewSerial) const noexcept
        {
            for (int s = kRows - 1; s >= 0; --s)
                for (const auto& e : rows[(size_t) s])
                    if (isActive(e.state.load(std::memory_order_seq_cst)) && reached(viewSerial, e.activation))
                        return s;
            return -1;
        }

        // Renders the slot's tails into slotOut[0, n), entry order, after the live layer. `ctx` is the block's
        // context for this slot; the bin makes it a tail context (retiring = 1, no MIDI in, no peers) and keeps
        // its midiOut (the caller's note-off reserve). In-place entries are skipped when the slot is live.
        // Returns true when an entry went Done this call (the caller requests deferred work for the free).
        bool render(int slot, RenderContext ctx, juce::AudioBuffer<float>& slotOut, int n,
                    uint32_t viewSerial, bool slotHasLive) noexcept
        {
            if (slot < 0 || slot >= kRows || n <= 0) return false;
            ctx.retiring = 1;
            ctx.midi.events = nullptr;
            ctx.midi.count = 0;
            ctx.peers = nullptr;
            ctx.numPeers = 0;

            bool exclusive = ! slotHasLive;   // a slot with no live layer arrives cleared: the first tail may write
            bool wentDone = false;
            for (auto& e : rows[(size_t) slot])
            {
                uint8_t st = e.state.load(std::memory_order_seq_cst);
                if (! isActive(st) || ! reached(viewSerial, e.activation)) continue;
                if (e.inPlace != 0 && slotHasLive) continue;   // the spare is the live layer now
                if (st == Parked)
                {
                    uint8_t expect = Parked;
                    if (! e.state.compare_exchange_strong(expect, (uint8_t) Draining, std::memory_order_seq_cst)) continue;
                    e.tail->retirePath.store(true, std::memory_order_relaxed);
                    e.tail->onRetireStart(ctx);
                    e.tail->retirePath.store(false, std::memory_order_relaxed);
                    e.rs.started = 1;
                }

                const bool alive = renderEntry(e, ctx, slotOut, n, exclusive);
                exclusive = false;
                if (! alive)
                {
                    uint8_t expect = Draining;
                    if (e.state.compare_exchange_strong(expect, (uint8_t) Done, std::memory_order_seq_cst))
                    {
                        wentDone = true;
                        pending.store(true, std::memory_order_seq_cst);
                    }
                }
            }
            return wentDone;
        }

        // Message thread, a structural edit with the audio thread excluded (layersLock held): fn(RetireTail&) for each
        // entry of the slot that still sounds (Parked or Draining), under binMutex so no service pass frees one meanwhile.
        // fn may only reset state the tail owns: it must not allocate, free, wait or take a lock.
        template <class Fn>
        void forEachActive(int slot, Fn&& fn)
        {
            if (slot < 0 || slot >= kRows) return;
            BinLock g(binMutex);
            for (auto& e : rows[(size_t) slot])
                if (e.tail != nullptr && isActive(e.state.load(std::memory_order_seq_cst)))
                    fn(*e.tail);
        }

        // ─── Inspection (tests; message thread) ──────────────────────────────────────────────────────
        State stateOf(int slot, int entry) const noexcept
        {
            return (State) rows[(size_t) slot][(size_t) entry].state.load(std::memory_order_seq_cst);
        }
        const RetireTail* tailOf(int slot, int entry) const noexcept { return rows[(size_t) slot][(size_t) entry].tail; }
        const RetireState& stateDataOf(int slot, int entry) const noexcept { return rows[(size_t) slot][(size_t) entry].rs; }
        bool isInPlace(int slot, int entry) const noexcept { return rows[(size_t) slot][(size_t) entry].inPlace != 0; }
        int  countIn(State s) const noexcept
        {
            int c = 0;
            for (const auto& row : rows)
                for (const auto& e : row) c += e.state.load(std::memory_order_seq_cst) == s ? 1 : 0;
            return c;
        }
        int  reclaimPending() const { BinLock g(binMutex); return reclaimCount; }
        // True on the thread that is freeing a tail inside the bin (a destructor probe asserts it).
        static bool isFreeingOnThisThread() noexcept { return freeingFlag(); }
        // True while the calling thread holds binMutex (a destructor probe asserts it is false: no free under it).
        static bool isMutexHeldOnThisThread() noexcept { return heldFlag() > 0; }

    private:
        struct Entry
        {
            std::atomic<uint8_t> state { Empty };
            uint8_t     inPlace = 0;         // written while Empty, published by the Parked store
            uint32_t    activation = 0;      // ditto; compared wrap-safely
            RetireTail* tail = nullptr;      // ditto
            RetireState rs {};               // ditto; the audio thread's after Parked
            RetirePolicy::Kind kind = RetirePolicy::Kind::None;   // ditto
            uint8_t     closeDeclick = 0;    // ditto: RetirePolicy::closeWithDeclick
            bool        doneSeen = false;    // message thread: service has stamped this Done
            uint64_t    doneStamp = 0;
            uint64_t    parkSeq = 0;         // message thread: park order (global-budget eviction)
            std::unique_ptr<RetireTail> owned;   // message thread only; null for an in-place entry
        };

        struct Reclaim
        {
            std::unique_ptr<RetireTail> owned;
            bool     needsGrace = false;     // the audio thread may still hold it
            bool     hasNotBefore = false;   // it stays live until the published serial reaches notBefore
            uint32_t notBefore = 0;
            bool     stamped = false;
            uint64_t stamp = 0;
        };

        static bool& freeingFlag() noexcept { static thread_local bool f = false; return f; }
        static int&  heldFlag() noexcept    { static thread_local int n = 0; return n; }

        // binMutex with the "held on this thread" mark the free path checks; unlock / lock for an eviction wait.
        class BinLock
        {
        public:
            explicit BinLock(std::mutex& m) : lock(m) { ++heldFlag(); }
            ~BinLock() { if (lock.owns_lock()) --heldFlag(); }
            void unlock() { lock.unlock(); --heldFlag(); }
            void relock() { lock.lock(); ++heldFlag(); }
        private:
            std::unique_lock<std::mutex> lock;
        };

        // The tails one call frees, destroyed when the call returns: declared BEFORE the call's BinLock, so they
        // are destroyed after it is released. Fixed size: every entry plus every reclaim slot, nothing allocated.
        class FreeList
        {
        public:
            FreeList() = default;
            ~FreeList()
            {
                jassert(! isMutexHeldOnThisThread());   // a tail destructor never runs under binMutex
                freeingFlag() = true;
                for (int i = 0; i < count; ++i) items[(size_t) i].reset();
                freeingFlag() = false;
            }
            FreeList(const FreeList&) = delete;
            FreeList& operator=(const FreeList&) = delete;
            void add(std::unique_ptr<RetireTail> t) noexcept
            {
                if (t == nullptr) return;
                jassert(count < (int) items.size());
                if (count < (int) items.size()) items[(size_t) count++] = std::move(t);
                else t.release();   // unreachable by construction; leaking beats freeing under the mutex
            }
        private:
            std::array<std::unique_ptr<RetireTail>, (size_t) (kEntries + kReclaimCapacity)> items;
            int count = 0;
        };

        static bool isActive(uint8_t s) noexcept { return s == Parked || s == Draining; }
        static bool reached(uint32_t viewSerial, uint32_t activation) noexcept
        {
            return (int32_t) (viewSerial - activation) >= 0;
        }

        bool hasActiveInPlaceLocked(int slot) const noexcept
        {
            for (const auto& e : rows[(size_t) slot])
                if (e.inPlace != 0 && isActive(e.state.load(std::memory_order_seq_cst))) return true;
            return false;
        }

        // The message thread's cut: Parked or Draining to Done (the audio thread may race it: one CAS wins).
        void cutLocked(Entry& e) noexcept
        {
            for (uint8_t from : { (uint8_t) Parked, (uint8_t) Draining })
            {
                uint8_t expect = from;
                if (e.state.compare_exchange_strong(expect, (uint8_t) Done, std::memory_order_seq_cst))
                {
                    pending.store(true, std::memory_order_seq_cst);
                    return;
                }
            }
        }

        void stampDoneLocked(Entry& e) noexcept
        {
            if (! e.doneSeen) { e.doneSeen = true; e.doneStamp = epoch.stamp(); }
        }

        // Done to Empty; returns the owned tail (null for an in-place entry) for the caller to dispose of.
        std::unique_ptr<RetireTail> releaseLocked(Entry& e) noexcept
        {
            auto owned = std::move(e.owned);
            e.tail = nullptr;
            e.inPlace = 0;
            e.doneSeen = false;
            e.doneStamp = 0;
            e.state.store(Empty, std::memory_order_seq_cst);
            return owned;
        }

        // An outgoing tail the bin did not take (NotParked, Dropped): it may be live until the store carrying
        // `activation` lands, so it goes to the reclaim list gated on that serial and a grace stamped after it. If the
        // list is full it is leaked (never freed early). Never destroyed inside the call.
        void retireLater(std::unique_ptr<RetireTail> t, uint32_t activation, FreeList& frees)
        {
            juce::ignoreUnused(frees);
            if (t == nullptr) return;
            if (reclaimCount >= kReclaimCapacity) { jassertfalse; t.release(); return; }
            auto& r = reclaim[(size_t) reclaimCount++];
            r.owned = std::move(t);
            r.needsGrace = true;
            r.hasNotBefore = true;
            r.notBefore = activation;
            r.stamped = false;
            r.stamp = 0;
            pending.store(true, std::memory_order_seq_cst);
        }

        // A tail this call no longer holds: freed after the call (FreeList), or, when the caller holds layersLock
        // or the audio thread may still read it, left in the reclaim list for a later service pass.
        void dispose(std::unique_ptr<RetireTail> t, bool needsGrace, bool audioExcluded, FreeList& frees)
        {
            if (t == nullptr) return;
            if (! needsGrace && ! audioExcluded) { frees.add(std::move(t)); return; }
            if (reclaimCount < kReclaimCapacity)
            {
                auto& r = reclaim[(size_t) reclaimCount++];
                r.owned = std::move(t);
                r.needsGrace = needsGrace;
                r.hasNotBefore = false;
                r.notBefore = 0;
                r.stamped = false;
                r.stamp = 0;
                pending.store(true, std::memory_order_seq_cst);
                return;
            }
            // The list holds twice the bin's entries, so this is unreachable in practice. A tail the audio thread
            // may still read is leaked rather than freed early; one it cannot reach is freed after the call.
            // Under the caller's layersLock (audioExcluded) nothing is freed either: a layer destructor must not run there.
            jassertfalse;
            if (needsGrace || audioExcluded) t.release();
            else                             frees.add(std::move(t));
        }

        // Evicts an entry the caller chose (cut, then empty it). Under audioExcluded the audio thread cannot be in
        // the bin: the entry is emptied at once and its tail goes to the reclaim list. Otherwise the entry is cut and,
        // if its grace has not passed, binMutex is RELEASED for a bounded wait and the state re-validated after the
        // re-lock (the entry may have been freed or re-used meanwhile). True = the entry is free for reuse now.
        bool evictLocked(Entry& e, bool audioExcluded, BinLock& g, FreeList& frees)
        {
            cutLocked(e);
            if (e.state.load(std::memory_order_seq_cst) == Empty) return true;
            if (audioExcluded) { dispose(releaseLocked(e), false, true, frees); return true; }
            if (epoch.reclaimDisabled()) return false;
            stampDoneLocked(e);
            const uint64_t stamp = e.doneStamp;
            const uint64_t seq = e.parkSeq;
            if (! epoch.passed(stamp))
            {
                const int timeout = evictTimeoutMs;
                g.unlock();
                const bool ok = epoch.waitFor(stamp, timeout);
                g.relock();
                if (! ok) return false;
                // Re-validate: still the entry we cut (same park), still Done? Else another pass freed or re-used it.
                if (e.state.load(std::memory_order_seq_cst) == Empty) return true;
                if (e.parkSeq != seq || e.state.load(std::memory_order_seq_cst) != Done) return false;
            }
            frees.add(releaseLocked(e));
            return true;
        }

        // The entry a park fills: the row's first Empty one, after freeing its Done entries whose grace passed.
        // A full row evicts entry 0 (mu-Clid's force-cut rule); the global budget evicts the instance's oldest.
        // May release binMutex during an eviction wait (see evictLocked); the row is re-scanned afterwards.
        Entry* placeLocked(int slot, bool audioExcluded, BinLock& g, FreeList& frees)
        {
            auto& row = rows[(size_t) slot];
            const bool canFree = ! epoch.reclaimDisabled();
            for (auto& e : row)
                if (e.state.load(std::memory_order_seq_cst) == Done)
                {
                    stampDoneLocked(e);
                    if (audioExcluded) dispose(releaseLocked(e), false, true, frees);
                    else if (canFree && epoch.passed(e.doneStamp)) frees.add(releaseLocked(e));
                }

            // An eviction may release binMutex for its wait, so the budget and the row are re-checked after each one
            // (another park may have filled them meanwhile); bounded, a failed eviction ends the attempt.
            for (int attempt = 0; attempt < kEntries + 1; ++attempt)
            {
                if (activeCountLocked() >= budget)
                {
                    Entry* oldest = oldestActiveLocked();
                    if (oldest == nullptr || ! evictLocked(*oldest, audioExcluded, g, frees)) return nullptr;
                    continue;
                }
                if (Entry* e = firstEmptyLocked(row)) return e;
                if (! evictLocked(row[0], audioExcluded, g, frees)) return nullptr;
            }
            return nullptr;
        }

        static Entry* firstEmptyLocked(std::array<Entry, kDepth>& row) noexcept
        {
            for (auto& e : row)
                if (e.state.load(std::memory_order_seq_cst) == Empty) return &e;
            return nullptr;
        }

        // Fills an Empty entry and publishes it (the Parked store is the release of the payload).
        void fillLocked(Entry& e, RetireTail& t, uint32_t activation, bool inPlace)
        {
            const auto pol = t.retirePolicy();
            const double sr = preparedRate > 0.0 ? preparedRate : 44100.0;
            const int ramp = juce::jmax(1, (int) (sr * (pol.fadeMs / 1000.0)));   // mu-Tant's retireRampSamples arithmetic
            const double life = juce::jlimit(0.0, mu_limits::kRetireHardCapSeconds, (double) pol.maxSeconds);
            e.tail = &t;
            e.inPlace = inPlace ? 1 : 0;
            e.activation = activation;
            e.rs = {};
            e.rs.samplesLeft = pol.kind == RetirePolicy::Kind::Fade ? ramp : 0;
            e.rs.gainStep = 1.0f / (float) ramp;
            e.rs.maxSamples = juce::jmax<int64_t>(1, (int64_t) (sr * life));
            e.kind = pol.kind;
            e.closeDeclick = pol.kind == RetirePolicy::Kind::Natural && pol.closeWithDeclick ? 1 : 0;
            e.doneSeen = false;
            e.doneStamp = 0;
            e.parkSeq = ++parkCounter;
            e.state.store(Parked, std::memory_order_seq_cst);
        }

        int activeCountLocked() const noexcept
        {
            int c = 0;
            for (const auto& row : rows)
                for (const auto& e : row) c += isActive(e.state.load(std::memory_order_seq_cst)) ? 1 : 0;
            return c;
        }

        Entry* oldestActiveLocked() noexcept
        {
            Entry* best = nullptr;
            for (auto& row : rows)
                for (auto& e : row)
                    if (isActive(e.state.load(std::memory_order_seq_cst)) && (best == nullptr || e.parkSeq < best->parkSeq))
                        best = &e;
            return best;
        }

        // Exchanges two entries' contents (structural edits, audio excluded).
        static void swapEntries(Entry& a, Entry& b) noexcept
        {
            const uint8_t sa = a.state.load(std::memory_order_seq_cst);
            a.state.store(b.state.load(std::memory_order_seq_cst), std::memory_order_seq_cst);
            b.state.store(sa, std::memory_order_seq_cst);
            std::swap(a.inPlace, b.inPlace);
            std::swap(a.activation, b.activation);
            std::swap(a.tail, b.tail);
            std::swap(a.rs, b.rs);
            std::swap(a.kind, b.kind);
            std::swap(a.closeDeclick, b.closeDeclick);
            std::swap(a.doneSeen, b.doneSeen);
            std::swap(a.doneStamp, b.doneStamp);
            std::swap(a.parkSeq, b.parkSeq);
            std::swap(a.owned, b.owned);
        }

        // One tail call: the fade countdown, the lifetime, and the declick in the last frames of the lifetime
        // (rendered into the bin's own prepared scratch so the gain applies to the tail alone). True = still alive.
        bool renderEntry(Entry& e, const RenderContext& ctx, juce::AudioBuffer<float>& out, int n, bool exclusive) noexcept
        {
            auto& rs = e.rs;
            const int64_t lifeLeft = rs.maxSamples - rs.samplesLived;
            const bool inDeclick = lifeLeft - (int64_t) n < (int64_t) declickFrames && n <= declick.getNumSamples();
            bool alive;
            if (! inDeclick)
            {
                rs.exclusive = exclusive ? 1 : 0;
                e.tail->retirePath.store(true, std::memory_order_relaxed);
                alive = e.tail->renderRetiring(ctx, out, n, rs);
                e.tail->retirePath.store(false, std::memory_order_relaxed);
            }
            else
            {
                const int nCh = juce::jmin(out.getNumChannels(), declick.getNumChannels());
                juce::AudioBuffer<float> priv(declick.getArrayOfWritePointers(), nCh, n);
                priv.clear();
                rs.exclusive = 1;
                e.tail->retirePath.store(true, std::memory_order_relaxed);
                alive = e.tail->renderRetiring(ctx, priv, n, rs);
                e.tail->retirePath.store(false, std::memory_order_relaxed);
                // Linear declick to zero at the end of the lifetime: gain = framesLeftOfLife / declickFrames.
                for (int c = 0; c < nCh; ++c)
                {
                    const float* src = priv.getReadPointer(c);
                    float* dst = out.getWritePointer(c);
                    for (int i = 0; i < n; ++i)
                    {
                        const double g = juce::jlimit(0.0, 1.0, (double) (lifeLeft - i) / (double) declickFrames);
                        dst[i] += (float) g * src[i];
                    }
                }
            }
            if (e.kind == RetirePolicy::Kind::Fade)
            {
                rs.samplesLeft = juce::jmax(0, rs.samplesLeft - n);
                if (rs.samplesLeft == 0) alive = false;
            }
            else if (e.closeDeclick != 0)
            {
                // A Natural tail whose silence report is a drain criterion, not a level (closeWithDeclick): it is not
                // cut there, its lifetime is shortened to this call plus the 10 ms declick, which then ramps whatever
                // it still renders to zero. Once armed, its own report no longer matters.
                if (rs.reserved[0] != 0)
                    alive = true;
                else if (! alive)
                {
                    rs.reserved[0] = 1;
                    rs.maxSamples = juce::jmin(rs.maxSamples, rs.samplesLived + (int64_t) n + (int64_t) declickFrames);
                    alive = true;
                }
            }
            rs.samplesLived += n;
            if (rs.samplesLived >= rs.maxSamples) alive = false;
            return alive;
        }

        const AudioEpoch& epoch;
        const std::atomic<uint64_t>& layout;   // the owner's packed layout word (LayoutWord)
        // The serial half of the layout word (LayoutWord::serial: the high 32 bits).
        uint32_t publishedSerial() const noexcept { return (uint32_t) (layout.load(std::memory_order_seq_cst) >> 32); }
        CountPolicy policy = CountPolicy::RenderPastCount;
        std::array<std::array<Entry, kDepth>, kRows> rows;
        std::array<Reclaim, kReclaimCapacity> reclaim;
        int reclaimCount = 0;                // guarded by binMutex
        mutable std::mutex binMutex;         // message and host threads only
        double preparedRate = 0.0;           // guarded by binMutex
        int    budget = kEntries;            // guarded by binMutex
        int    evictTimeoutMs = 50;          // guarded by binMutex
        uint64_t parkCounter = 0;            // guarded by binMutex
        juce::AudioBuffer<float> declick;    // audio thread's after prepare; sized at prepare only
        int declickFrames = 1;               // written at prepare (not processing), read by the audio thread
        std::atomic<bool> pending { false };
    };
}
