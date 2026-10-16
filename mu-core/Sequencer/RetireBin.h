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
// false, (b) a Fade policy's ramp is spent, (c) its hard lifetime (maxSeconds, clamped to 30 s) is over, with
// a 10 ms linear declick in its last frames, or (d) the message thread cuts it. A tail that never reports
// silence (an engine with nothing loaded that never says drained) therefore still ends at (c). Done entries
// are freed only by service(), on the message thread, outside layersLock, once the grace passed: it runs at
// the top of each deferred-work pass (which the audio thread requests when an entry goes Done), on the
// 100 Hz poll while hasPendingWork(), on the headless flush, and at prepare / release / destruction.
//
// Locks: every message-side operation takes binMutex (never the audio thread, never layersLock). Lock order
// layoutWriteLock, layersLock, binMutex; nobody takes layersLock while holding binMutex. A RetireTail
// destructor runs under binMutex (service) and must take no lock.
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
            NotParked,       // the tail's policy is None: it is freed after the grace period
            AlreadyActive,   // parkInPlace on a row that already has an active in-place entry: no-op
            Dropped,         // the row was full and its eviction timed out: the incoming tail is freed after its grace
            Invalid          // slot out of range or no tail
        };

        static constexpr int kRows = mu_limits::kMaxLayers;
        static constexpr int kDepth = mu_limits::kRetireDepth;
        static constexpr int kReclaimCapacity = kRows * kDepth;

        explicit RetireBin(const AudioEpoch& e) noexcept : epoch(e) {}
        ~RetireBin() = default;   // owners call cutAll + service from prepareForDestruction first
        RetireBin(const RetireBin&) = delete;
        RetireBin& operator=(const RetireBin&) = delete;

        // ─── Configuration (message thread, before processing) ───────────────────────────────────────
        void        setCountPolicy(CountPolicy p) noexcept { policy = p; }
        CountPolicy countPolicy() const noexcept           { return policy; }
        // Tails sounding at once in this instance; when reached, a park evicts the oldest active entry.
        void setGlobalBudget(int maxLive) { std::lock_guard<std::mutex> g(binMutex); budget = juce::jlimit(1, kReclaimCapacity, maxLive); }
        // How long an eviction outside layersLock waits for the grace period before dropping the incoming tail.
        void setEvictTimeoutMs(int ms) { std::lock_guard<std::mutex> g(binMutex); evictTimeoutMs = juce::jmax(0, ms); }

        // prepareToPlay: the rate park converts seconds with, and the declick scratch (the host is not
        // processing). Allocates on the calling (message or host) thread.
        void setPrepared(double sampleRate, int blockSize)
        {
            std::lock_guard<std::mutex> g(binMutex);
            preparedRate = sampleRate;
            declick.setSize(2, juce::jmax(1, blockSize), false, true, false);
            declickFrames = juce::jmax(1, (int) (sampleRate * (mu_limits::kRetireDeclickMs / 1000.0)));
        }

        // ─── Message side (every one takes binMutex) ─────────────────────────────────────────────────
        // An owned tail (a whole layer, an engine). `audioExcluded`: the caller holds layersLock in TryLock
        // mode, so the audio thread cannot be inside the bin; an eviction then frees at once, without a wait.
        ParkResult park(int slot, std::unique_ptr<RetireTail> owned, uint32_t activation, bool audioExcluded)
        {
            if (slot < 0 || slot >= kRows || owned == nullptr) return ParkResult::Invalid;
            std::unique_lock<std::mutex> g(binMutex);
            if (owned->retirePolicy().kind == RetirePolicy::Kind::None)
            {
                addReclaimLocked(std::move(owned), ! audioExcluded);   // it may have been live: free after the grace
                return ParkResult::NotParked;
            }
            Entry* e = placeLocked(slot, audioExcluded);
            if (e == nullptr)
            {
                addReclaimLocked(std::move(owned), false);   // never visible to the audio thread
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
            std::unique_lock<std::mutex> g(binMutex);
            for (auto& e : rows[(size_t) slot])
                if (e.inPlace != 0 && isActive(e.state.load(std::memory_order_seq_cst)))
                    return ParkResult::AlreadyActive;
            if (spare.retirePolicy().kind == RetirePolicy::Kind::None) return ParkResult::NotParked;
            Entry* e = placeLocked(slot, audioExcluded);
            if (e == nullptr) return ParkResult::Dropped;
            fillLocked(*e, spare, activation, true);
            return ParkResult::Parked;
        }

        // Every active entry of the slot to Done: a remove, a regrow into an in-place slot (AFTER the count
        // store), before a spare is destroyed.
        void cutSlot(int slot)
        {
            if (slot < 0 || slot >= kRows) return;
            std::lock_guard<std::mutex> g(binMutex);
            for (auto& e : rows[(size_t) slot]) cutLocked(e);
        }

        // CutAtCount products: a count-reducing store cuts the rows it leaves (after the store).
        void cutAtOrAbove(int count)
        {
            std::lock_guard<std::mutex> g(binMutex);
            for (int s = juce::jmax(0, count); s < kRows; ++s)
                for (auto& e : rows[(size_t) s]) cutLocked(e);
        }

        // Structural edits, with the audio thread excluded (layersLock in TryLock mode, quiesce in Published):
        // owned tails follow their slot; in-place entries are CUT, never moved (their spare does not move).
        void swapRows(int a, int b)
        {
            if (a == b || a < 0 || b < 0 || a >= kRows || b >= kRows) return;
            std::lock_guard<std::mutex> g(binMutex);
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
            std::lock_guard<std::mutex> g(binMutex);
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
            std::lock_guard<std::mutex> g(binMutex);
            for (auto& row : rows)
                for (auto& e : row) cutLocked(e);
        }

        // Frees every Done entry (and reclaim-list tail) whose grace has passed. Never waits; an entry whose
        // grace has not passed waits for the next pass. Message thread, outside layersLock. Returns the count freed.
        int service()
        {
            std::lock_guard<std::mutex> g(binMutex);
            // Cleared before the scan: an entry the audio thread marks Done during it raises the flag again.
            pending.store(false, std::memory_order_seq_cst);
            int freed = 0;
            const bool canFree = ! epoch.reclaimDisabled();
            bool left = false;
            for (auto& row : rows)
                for (auto& e : row)
                {
                    if (e.state.load(std::memory_order_seq_cst) != Done) continue;
                    if (! e.doneSeen) { e.doneSeen = true; e.doneStamp = epoch.stamp(); }
                    if (canFree && epoch.passed(e.doneStamp)) { releaseLocked(e, false); ++freed; }
                    else left = true;
                }
            for (int i = 0; i < reclaimCount; )
            {
                auto& r = reclaim[(size_t) i];
                if (r.needsGrace && ! r.stamped) { r.stamped = true; r.stamp = epoch.stamp(); }
                const bool ok = ! r.needsGrace || (canFree && epoch.passed(r.stamp));
                if (ok)
                {
                    { FreeingScope f; r.owned.reset(); }
                    reclaim[(size_t) i] = std::move(reclaim[(size_t) (reclaimCount - 1)]);
                    --reclaimCount;
                    ++freed;
                }
                else { left = true; ++i; }
            }
            if (left) pending.store(true, std::memory_order_seq_cst);
            return freed;
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
                    e.tail->retirePath = true;
                    e.tail->onRetireStart(ctx);
                    e.tail->retirePath = false;
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
        int  reclaimPending() const { std::lock_guard<std::mutex> g(binMutex); return reclaimCount; }
        // True on the thread that is freeing a tail inside the bin (a destructor probe asserts it).
        static bool isFreeingOnThisThread() noexcept { return freeingFlag(); }

    private:
        struct Entry
        {
            std::atomic<uint8_t> state { Empty };
            uint8_t     inPlace = 0;         // written while Empty, published by the Parked store
            uint32_t    activation = 0;      // ditto; compared wrap-safely
            RetireTail* tail = nullptr;      // ditto
            RetireState rs {};               // ditto; the audio thread's after Parked
            RetirePolicy::Kind kind = RetirePolicy::Kind::None;   // ditto
            bool        doneSeen = false;    // message thread: service has stamped this Done
            uint64_t    doneStamp = 0;
            uint64_t    parkSeq = 0;         // message thread: park order (global-budget eviction)
            std::unique_ptr<RetireTail> owned;   // message thread only; null for an in-place entry
        };

        struct Reclaim
        {
            std::unique_ptr<RetireTail> owned;
            bool     needsGrace = false;     // the audio thread may still hold it
            bool     stamped = false;
            uint64_t stamp = 0;
        };

        // Marks the calling thread as freeing a tail (the destructor probe of the tests reads it).
        struct FreeingScope
        {
            FreeingScope() noexcept  { freeingFlag() = true; }
            ~FreeingScope() noexcept { freeingFlag() = false; }
        };
        static bool& freeingFlag() noexcept { static thread_local bool f = false; return f; }

        static bool isActive(uint8_t s) noexcept { return s == Parked || s == Draining; }
        static bool reached(uint32_t viewSerial, uint32_t activation) noexcept
        {
            return (int32_t) (viewSerial - activation) >= 0;
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

        // Done to Empty. `deferFree`: the owned tail goes to the reclaim list (freed by a later service, never
        // under the caller's layersLock); otherwise it is destroyed here, under binMutex.
        void releaseLocked(Entry& e, bool deferFree)
        {
            auto owned = std::move(e.owned);
            e.tail = nullptr;
            e.inPlace = 0;
            e.doneSeen = false;
            e.doneStamp = 0;
            e.state.store(Empty, std::memory_order_seq_cst);
            if (owned == nullptr) return;
            if (deferFree) addReclaimLocked(std::move(owned), false);
            else { FreeingScope f; owned.reset(); }
        }

        void addReclaimLocked(std::unique_ptr<RetireTail> owned, bool needsGrace)
        {
            if (owned == nullptr) return;
            if (reclaimCount >= kReclaimCapacity)
            {
                // The list is full (it holds at most one bin's worth): wait out a grace here, bounded, then free.
                // A timeout, or reclamation turned off, leaks the tail rather than risk a use-after-free.
                const bool safe = ! needsGrace
                               || (! epoch.reclaimDisabled() && epoch.waitFor(epoch.stamp(), evictTimeoutMs));
                if (safe) { FreeingScope f; owned.reset(); }
                else      owned.release();
                return;
            }
            auto& r = reclaim[(size_t) reclaimCount++];
            r.owned = std::move(owned);
            r.needsGrace = needsGrace;
            r.stamped = false;
            r.stamp = 0;
            pending.store(true, std::memory_order_seq_cst);
        }

        // Frees (or detaches) an entry the caller has decided to evict. Under audioExcluded the audio thread
        // cannot be in the bin: the entry is emptied at once and its tail freed later outside the lock. Otherwise
        // the entry is cut and one grace period is waited, bounded; false = timed out (the entry stays Done).
        bool evictLocked(Entry& e, bool audioExcluded)
        {
            cutLocked(e);
            if (e.state.load(std::memory_order_seq_cst) == Empty) return true;
            if (audioExcluded) { releaseLocked(e, true); return true; }
            if (epoch.reclaimDisabled()) return false;
            if (! e.doneSeen) { e.doneSeen = true; e.doneStamp = epoch.stamp(); }
            if (! epoch.waitFor(e.doneStamp, evictTimeoutMs)) return false;
            releaseLocked(e, false);
            return true;
        }

        // The entry a park fills: the row's first Empty one, after freeing its Done entries whose grace passed.
        // A full row evicts entry 0 (mu-Clid's force-cut rule); the global budget evicts the instance's oldest.
        Entry* placeLocked(int slot, bool audioExcluded)
        {
            auto& row = rows[(size_t) slot];
            const bool canFree = ! epoch.reclaimDisabled();
            for (auto& e : row)
                if (e.state.load(std::memory_order_seq_cst) == Done)
                {
                    if (! e.doneSeen) { e.doneSeen = true; e.doneStamp = epoch.stamp(); }
                    if (audioExcluded) releaseLocked(e, true);
                    else if (canFree && epoch.passed(e.doneStamp)) releaseLocked(e, false);
                }

            if (activeCountLocked() >= budget)
                if (Entry* oldest = oldestActiveLocked())
                    if (! evictLocked(*oldest, audioExcluded)) return nullptr;

            for (auto& e : row)
                if (e.state.load(std::memory_order_seq_cst) == Empty) return &e;
            return evictLocked(row[0], audioExcluded) ? &row[0] : nullptr;
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
                e.tail->retirePath = true;
                alive = e.tail->renderRetiring(ctx, out, n, rs);
                e.tail->retirePath = false;
            }
            else
            {
                const int nCh = juce::jmin(out.getNumChannels(), declick.getNumChannels());
                juce::AudioBuffer<float> priv(declick.getArrayOfWritePointers(), nCh, n);
                priv.clear();
                rs.exclusive = 1;
                e.tail->retirePath = true;
                alive = e.tail->renderRetiring(ctx, priv, n, rs);
                e.tail->retirePath = false;
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
            rs.samplesLived += n;
            if (rs.samplesLived >= rs.maxSamples) alive = false;
            return alive;
        }

        const AudioEpoch& epoch;
        CountPolicy policy = CountPolicy::RenderPastCount;
        std::array<std::array<Entry, kDepth>, kRows> rows;
        std::array<Reclaim, kReclaimCapacity> reclaim;
        int reclaimCount = 0;                // guarded by binMutex
        mutable std::mutex binMutex;         // message and host threads only
        double preparedRate = 0.0;           // guarded by binMutex
        int    budget = kReclaimCapacity;    // guarded by binMutex
        int    evictTimeoutMs = 50;          // guarded by binMutex
        uint64_t parkCounter = 0;            // guarded by binMutex
        juce::AudioBuffer<float> declick;    // audio thread's after prepare; sized at prepare only
        int declickFrames = 1;               // written at prepare (not processing), read by the audio thread
        std::atomic<bool> pending { false };
    };
}
