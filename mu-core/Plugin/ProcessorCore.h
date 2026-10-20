#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Plugin/HostTransport.h"
#include "Plugin/LayerBlock.h"
#include "Sequencer/Layer.h"
#include "Sequencer/LayerTypeInfo.h"   // LayerServices, kLayerTypeIdCount

#include <atomic>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

// The slim parent of ProcessorBase: the layer storage and accessors and the published host transport.
// It depends on nothing but JUCE, Layer and HostTransport, so a unit test can derive a small
// fake from it (mu-core/Tests) without linking the mixer, FX, voice or licensing sources.
class ProcessorCore : public juce::AudioProcessor
{
public:
    explicit ProcessorCore(const BusesProperties& props) : juce::AudioProcessor(props) {}

    // ─── Channel metadata for shared mixer UI ────────────────────────────────
    // Each mu-family plugin has some N "channels" (rhythms in mu-clid; whatever
    // the trigger model dictates in mu-tant / mu-toni) — each with a display
    // name and a palette colour index. The shared MixerOverlay / MixerChannel
    // UI calls these to label channel strips, populate sidechain-source
    // dropdowns, etc., without needing to know what a channel actually IS.
    // Defaults read the owned layer storage below; a product that has not moved its layers
    // into it yet overrides both.
    virtual int         getNumLayers()              const { return numLayers.load(std::memory_order_relaxed); }
    // Layer `i` (name, colour, modulation) or nullptr when `i` is out of range. Message thread only.
    virtual Layer*       getLayer(int i)
    {
        return (i >= 0 && i < getNumLayers()) ? layers[(size_t) i].get() : nullptr;
    }
    const Layer*         getLayer(int i) const { return const_cast<ProcessorCore*>(this)->getLayer(i); }
    // The most channels the product can ever have (1..8) — sizes the program-change
    // channel table. Products with a fixed layer count return that count.
    virtual int         getMaxLayers()              const { return 8; }
    virtual juce::String getLayerName(int idx)       const = 0;
    virtual int         getLayerColourIndex(int idx) const = 0;

    // Host transport as last seen by processBlock, for the UI (TransportBar). The UI must never
    // call getPlayHead() itself: the playhead is only valid on the audio thread during a block.
    bool isHostPlaying() const noexcept { return hostPlaying.load(std::memory_order_relaxed); }
    // Host meter (4/4 when the host gives none) and current bar start; false = no bar start.
    void getHostTimeSignature(int& numerator, int& denominator) const noexcept
    {
        const int packed = hostTimeSig.load(std::memory_order_relaxed);
        numerator   = packed >> 8;
        denominator = packed & 0xFF;
    }
    bool getHostBarStartPpq(double& ppq) const noexcept
    {
        const double v = hostBarStart.load(std::memory_order_relaxed);
        if (std::isnan(v)) return false;
        ppq = v;
        return true;
    }
    // Host beat position in quarter notes; false when the host supplied none.
    bool getHostPpqPosition(double& ppq) const noexcept
    {
        const double v = hostPpq.load(std::memory_order_relaxed);
        if (std::isnan(v)) return false;
        ppq = v;
        return true;
    }

    // A host bypass still publishes the host transport, so the TransportBar keeps following
    // the DAW while the plugin is bypassed.
    using juce::AudioProcessor::processBlockBypassed;
    void processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        pollHostTransport();
        juce::AudioProcessor::processBlockBypassed(buffer, midi);
    }

    // Layer `i` as the product's concrete type, for any slot 0..kMaxLayers-1 whether or not it is
    // live yet (a preset can load data into a slot above the current count). The slot must be filled.
    // Only bounds and non-null are asserted: a dynamic_cast here would run on the audio thread in
    // every Debug block, so the product checks the type once when it builds its layers.
    template <class T> T& layerAs(int i) noexcept
    {
        jassert(i >= 0 && i < mu_limits::kMaxLayers && layers[(size_t) i] != nullptr);
        return static_cast<T&>(*layers[(size_t) i]);
    }
    template <class T> const T& layerAs(int i) const noexcept
    {
        return const_cast<ProcessorCore*>(this)->layerAs<T>(i);
    }

protected:
    // What each layer type shares between its layers (design 2.4), one slot per LayerTypeId, null until the first layer
    // of the type is added. Declared BEFORE the layers on purpose: members are destroyed in reverse order, so every
    // layer (and, because ProcessorBase's retire bin is destroyed before this class, every retired tail) is gone before
    // the services it read. The guard covers the table only (creating, finding, preparing); a service's own state has
    // its own rules. Message or host thread; the audio thread never touches it.
    std::array<std::unique_ptr<mu_core::LayerServices>, (size_t) mu_core::kLayerTypeIdCount> layerServices;
    mutable juce::CriticalSection layerServicesLock;

    // Layer storage: fixed capacity, runtime count. A product whose count or layer order changes at
    // runtime must have its audio thread take a ScopedTryLock on `layersLock` once per block and read
    // layers only inside it (mu-Clid, mu-Tant, mu-Toni and mu-On all do, through LayerBlock); the message thread holds it
    // only to change the count or move layers, and constructs and destroys layers outside it.
    std::array<std::unique_ptr<Layer>, mu_limits::kMaxLayers> layers;
    std::atomic<int>      numLayers { 0 };
public:
    // True for the whole of a structural layer edit (pointer moves and the parameter moves after
    // them); a product whose layer data is the parameter truth (layerHoldsParamTruth) gets it lowered
    // before the parameter push. A product's audio thread skips rendering while it is set, so hosts
    // whose wrapper does not honour suspendProcessing (CLAP) never play a moved layer with
    // half-shifted parameters. A flag, not a counter: edits never nest.
    std::atomic<bool>     layerEditInFlight { false };
protected:
    juce::CriticalSection layersLock;

    // True when the product keeps every slot up to kMaxLayers filled for the processor's lifetime
    // (a preset can load data into a slot above the count). Structural edits then refill the slot
    // they vacate. Set once in the product's constructor.
    bool keepSpareLayers = false;

    // Pointer moves for the structural edits (ProcessorBase::addLayer and the rest). Message thread
    // only, and the caller holds layersLock: they only move pointers and publish the count, so
    // they take microseconds and neither allocate nor destroy. A layer that falls out is returned
    // so the caller releases it after unlocking.
    //   publishLayerLocked   puts `fresh` in slot `numLayers` and grows the count; returns the
    //                        spare it replaced (null when the slot was empty)
    //   unpublishLayerLocked takes layer `idx` out, shifts the ones above it down, shrinks the
    //                        count and puts `spare` (which may be null) in the vacated top slot
    //   swapLayersLocked     exchanges two live layers
    // Both also hold layoutWriteLock (taken by the caller BEFORE layersLock): their count store advances the
    // layout serial.
    std::unique_ptr<Layer> publishLayerLocked(std::unique_ptr<Layer> fresh)
    {
        const int n = numLayers.load(std::memory_order_relaxed);
        jassert(n < mu_limits::kMaxLayers);
        auto replaced = std::move(layers[(size_t) n]);
        layers[(size_t) n] = std::move(fresh);
        storeLayerCount(n + 1, std::memory_order_release);
        return replaced;
    }
    std::unique_ptr<Layer> unpublishLayerLocked(int idx, std::unique_ptr<Layer> spare)
    {
        const int n = numLayers.load(std::memory_order_relaxed);
        jassert(idx >= 0 && idx < n);
        auto removed = std::move(layers[(size_t) idx]);
        for (int d = idx; d < n - 1; ++d)
            layers[(size_t) d] = std::move(layers[(size_t) (d + 1)]);
        layers[(size_t) (n - 1)] = std::move(spare);
        storeLayerCount(n - 1, std::memory_order_release);
        return removed;
    }

    // ─── Layer access for the render contract (docs/mu-tronic/design.md 2.1.2, 2.1.3) ────────────
    // The audio thread reaches layers through one mu_core::LayerBlock built from this state. The mode is
    // chosen once, in the product's constructor (TryLock: today's try-lock of layersLock, for the four
    // products; Published: the immutable published view, for a host that changes types without muting).
    void setLayerAccess(mu_core::LayerAccess mode) noexcept { layerAccess.mode = mode; }
    mu_core::LayerAccess getLayerAccess() const noexcept     { return layerAccess.mode; }

    // The packed layout word (count | serial << 32). EVERY live-count store goes through storeLayerCount, which
    // advances the serial by a compare-exchange loop (never a load then a store) and mirrors the count into
    // numLayers for the existing readers. The serial a park outside layersLock attaches is nextLayoutSerial():
    // the serial the following count store will carry, so nextLayoutSerial() and that store must run inside
    // ONE hold of layoutWriteLock (a message / host-thread lock, never taken by the audio thread; lock order
    // layoutWriteLock, layersLock, binMutex). Returns the serial it stored.
    uint32_t storeLayerCount(int n, std::memory_order mirrorOrder = std::memory_order_seq_cst) noexcept
    {
        jassert(n >= 0 && n <= mu_limits::kMaxLayers);
        uint64_t w = layerAccess.layout.load(std::memory_order_seq_cst);
        uint64_t next;
        do
            next = mu_core::LayoutWord::pack((uint32_t) n, mu_core::LayoutWord::serial(w) + 1u);
        while (! layerAccess.layout.compare_exchange_weak(w, next, std::memory_order_seq_cst));
        numLayers.store(n, mirrorOrder);
        return mu_core::LayoutWord::serial(next);
    }
    uint32_t layoutSerial() const noexcept     { return mu_core::LayoutWord::serial(layerAccess.layout.load(std::memory_order_seq_cst)); }
    uint32_t nextLayoutSerial() const noexcept { return layoutSerial() + 1u; }

    mu_core::LayerAccessState layerAccess;
    juce::CriticalSection     layoutWriteLock;
    void swapLayersLocked(int a, int b) noexcept
    {
        jassert(a >= 0 && b >= 0 && a < numLayers.load() && b < numLayers.load());
        std::swap(layers[(size_t) a], layers[(size_t) b]);
    }

    // Reads the host playhead for this block and publishes it for the UI. Call it from
    // processBlock in place of mu_core::readHostTransport(getPlayHead()) — audio thread only.
    mu_core::HostTransport pollHostTransport()
    {
        const auto t = mu_core::readHostTransport(getPlayHead());
        hostPlaying.store(t.playing, std::memory_order_relaxed);
        hostPpq.store(t.hasPosition ? t.ppqPosition : std::numeric_limits<double>::quiet_NaN(),
                      std::memory_order_relaxed);
        hostTimeSig.store((juce::jlimit(1, 255, t.timeSigNumerator) << 8) | juce::jlimit(1, 255, t.timeSigDenominator),
                          std::memory_order_relaxed);
        hostBarStart.store(t.hasBarStart ? t.barStartPpq : std::numeric_limits<double>::quiet_NaN(),
                           std::memory_order_relaxed);
        return t;
    }

private:
    // Host transport published by pollHostTransport(); NaN position = none supplied. One value
    // per atomic so the UI never sees a torn position/has-position pair.
    std::atomic<bool>   hostPlaying { false };
    std::atomic<double> hostPpq     { std::numeric_limits<double>::quiet_NaN() };
    std::atomic<int>    hostTimeSig { (4 << 8) | 4 };   // numerator << 8 | denominator, one value so it can't tear
    std::atomic<double> hostBarStart { std::numeric_limits<double>::quiet_NaN() };
    static_assert(std::atomic<double>::is_always_lock_free, "the UI reads atomic<double> transport values");
};
