#pragma once

#include "Plugin/AudioEpoch.h"
#include "Sequencer/Layer.h"
#include "MuLimits.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

// The audio thread's one way to reach layers (docs/mu-tronic/design.md 2.1.2, C1). A block constructs one
// LayerBlock on its stack; while it lives the block may read the LayerView it filled, and nothing else.
// No Layer*, view or tail pointer is kept past the block.
//
// Two modes, chosen once per processor at construction:
//   TryLock    today's protocol, unchanged: a try-lock of layersLock held for the whole block plus the
//              layerEditInFlight check. On success the view is filled from layers[] and the packed layout word.
//              The four products run this mode through the extraction programme.
//   Published  the audio thread never takes layersLock: after the layerEditInFlight check it loads one
//              immutable published view (a pool of kLayerViewPool views reused only after their AudioEpoch
//              grace period). For a host that changes layer types without muting (mu-Tronic, T1).
namespace mu_core
{
    enum class LayerAccess : uint8_t { TryLock, Published };

    // The layout as one block sees it. Immutable once published; never written by the audio thread.
    struct LayerView
    {
        uint32_t serial  = 0;                           // layout serial: changes on every structural edit and publication
        int32_t  numLive = 0;                           // slots 0..numLive-1 render
        uint32_t frozenMask = 0;                        // bit s: slot s renders from its last resolved values (scene provision)
        std::array<Layer*, mu_limits::kMaxLayers> layer {};   // every slot, spares included; null = no object
    };

    // The packed layout word: the live layer count in the low 32 bits and the layout serial in the high 32,
    // so a block reads both in one load. Every count store goes through it (ProcessorCore::storeLayerCount).
    struct LayoutWord
    {
        static constexpr uint64_t pack(uint32_t count, uint32_t serial) noexcept { return ((uint64_t) serial << 32) | count; }
        static constexpr uint32_t count(uint64_t w) noexcept  { return (uint32_t) (w & 0xffffffffu); }
        static constexpr uint32_t serial(uint64_t w) noexcept { return (uint32_t) (w >> 32); }
    };

    // Published mode's views: a pool reused only after the grace period of the stamp taken when each view
    // was replaced. Writers (message or host thread) call publish under the writers' mutex (layersLock in
    // that mode); the audio thread only loads. When the pool has no view past its grace, the writer
    // allocates an overflow view on its own thread instead of waiting (counted; none expected).
    class LayerViewPublisher
    {
    public:
        LayerViewPublisher() { pool[0].inUse = true; published.store(&pool[0].view, std::memory_order_seq_cst); }
        LayerViewPublisher(const LayerViewPublisher&) = delete;
        LayerViewPublisher& operator=(const LayerViewPublisher&) = delete;

        // Audio thread, once per block (inside the epoch).
        const LayerView* load() const noexcept { return published.load(std::memory_order_seq_cst); }

        // Writer thread: `next` becomes the view every later block reads. Returns the published copy.
        const LayerView* publish(const LayerView& next, const AudioEpoch& epoch)
        {
            reclaim(epoch);
            const LayerView* prev = published.load(std::memory_order_seq_cst);
            LayerView* dst = nullptr;
            for (auto& p : pool)
                if (! p.inUse) { p.inUse = true; p.retired = false; dst = &p.view; break; }
            if (dst == nullptr)
            {
                overflow.push_back({ std::make_unique<LayerView>(), 0, false });   // rt-ok: writer thread, never the audio block
                dst = overflow.back().view.get();
                ++overflowAllocations;
            }
            *dst = next;
            published.store(dst, std::memory_order_seq_cst);
            const uint64_t s = epoch.stamp();   // after the store: passed(s) proves no block still reads prev
            retire(prev, s);
            return dst;
        }

        // Writer thread: views whose grace passed become reusable (overflow views are freed).
        void reclaim(const AudioEpoch& epoch)
        {
            if (epoch.reclaimDisabled()) return;
            const LayerView* cur = published.load(std::memory_order_seq_cst);
            for (auto& p : pool)
                if (p.inUse && p.retired && &p.view != cur && epoch.passed(p.stamp)) { p.inUse = false; p.retired = false; }
            for (size_t i = 0; i < overflow.size(); )
            {
                auto& o = overflow[i];
                if (o.retired && o.view.get() != cur && epoch.passed(o.stamp)) overflow.erase(overflow.begin() + (std::ptrdiff_t) i);
                else ++i;
            }
        }

        int viewsInGrace() const noexcept
        {
            int n = 0;
            for (const auto& p : pool) n += (p.inUse && p.retired) ? 1 : 0;
            for (const auto& o : overflow) n += o.retired ? 1 : 0;
            return n;
        }
        int overflowCount() const noexcept { return overflowAllocations; }

    private:
        struct PoolView { LayerView view; uint64_t stamp = 0; bool inUse = false; bool retired = false; };
        struct OverflowView { std::unique_ptr<LayerView> view; uint64_t stamp = 0; bool retired = false; };

        void retire(const LayerView* v, uint64_t s) noexcept
        {
            for (auto& p : pool)
                if (&p.view == v) { p.retired = true; p.stamp = s; return; }
            for (auto& o : overflow)
                if (o.view.get() == v) { o.retired = true; o.stamp = s; return; }
        }

        std::array<PoolView, mu_limits::kLayerViewPool> pool;
        std::vector<OverflowView> overflow;   // writer thread only
        int overflowAllocations = 0;          // writer thread only
        std::atomic<const LayerView*> published { nullptr };
    };

    // The per-processor state behind LayerBlock (ProcessorCore owns one).
    struct LayerAccessState
    {
        LayerAccess           mode = LayerAccess::TryLock;   // set once, at construction
        std::atomic<uint64_t> layout { 0 };                  // LayoutWord: count | serial << 32
        AudioEpoch            epoch;
        LayerViewPublisher    views;                         // Published mode only
        static_assert(std::atomic<uint64_t>::is_always_lock_free, "the layout word is read on the audio thread");
    };

    // One block's access to the layers. Audio thread, stack only. The constructor enters the epoch, then
    // acquires (TryLock or Published); a false LayerBlock renders nothing and touches no bin entry, while the
    // transport and the boundary flagging (cached loop lengths) still advance. It also owns the block's
    // ScopedNoDenormals, so the layers, the bin, the mixer and the FX all run with denormals flushed (C11).
    class LayerBlock
    {
    public:
        LayerBlock(LayerAccessState& state, juce::CriticalSection& layersLock, const std::atomic<bool>& editInFlight,
                   const std::array<std::unique_ptr<Layer>, mu_limits::kMaxLayers>& layers) noexcept
            : access(state), lock(layersLock)
        {
            access.epoch.enter();   // rt-ok: epoch counter, not a lock
            if (access.mode == LayerAccess::TryLock)
            {
                if (! lock.tryEnter()) return;
                locked = true;
                if (editInFlight.load(std::memory_order_seq_cst)) { lock.exit(); locked = false; return; }
                const uint64_t w = access.layout.load(std::memory_order_seq_cst);
                local.serial = LayoutWord::serial(w);
                local.numLive = (int32_t) juce::jmin<uint32_t>(LayoutWord::count(w), (uint32_t) mu_limits::kMaxLayers);
                local.frozenMask = 0;
                for (int s = 0; s < mu_limits::kMaxLayers; ++s) local.layer[(size_t) s] = layers[(size_t) s].get();
                current = &local;
            }
            else
            {
                // Published: the flag after enter() and before the view load (all seq_cst) pairs with quiesce's
                // flag store and stamp, so a block that starts during an edit renders nothing (N4).
                if (editInFlight.load(std::memory_order_seq_cst)) return;
                current = access.views.load();
            }
        }

        ~LayerBlock() noexcept
        {
            if (locked) lock.exit();
            access.epoch.leave();
        }

        LayerBlock(const LayerBlock&) = delete;
        LayerBlock& operator=(const LayerBlock&) = delete;

        explicit operator bool() const noexcept { return current != nullptr; }
        const LayerView& view() const noexcept { jassert(current != nullptr); return *current; }
        LayerAccess mode() const noexcept { return access.mode; }

    private:
        juce::ScopedNoDenormals noDenormals;   // first member: covers everything the block renders
        LayerAccessState& access;
        juce::CriticalSection& lock;
        bool locked = false;
        LayerView local;                       // TryLock mode's view, filled under the lock
        const LayerView* current = nullptr;
    };
}
