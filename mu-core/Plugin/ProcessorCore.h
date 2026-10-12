#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Plugin/HostTransport.h"
#include "Sequencer/Layer.h"

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
    virtual int         getNumChannels()              const { return numLayers.load(std::memory_order_relaxed); }
    // Layer `i` (name, colour, modulation) or nullptr when `i` is out of range. Message thread only.
    virtual Layer*       getLayer(int i)
    {
        return (i >= 0 && i < getNumChannels()) ? layers[(size_t) i].get() : nullptr;
    }
    const Layer*         getLayer(int i) const { return const_cast<ProcessorCore*>(this)->getLayer(i); }
    // The most channels the product can ever have (1..8) — sizes the program-change
    // channel table. Products with a fixed layer count return that count.
    virtual int         getMaxChannels()              const { return 8; }
    virtual juce::String getChannelName(int idx)       const = 0;
    virtual int         getChannelColourIndex(int idx) const = 0;

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
    // Layer storage: fixed capacity, runtime count. A product whose count or layer order changes at
    // runtime must have its audio thread take a ScopedTryLock on `layersLock` once per block and read
    // layers only inside it (mu-Tant does); the message thread holds it only to change the count or
    // move layers, and constructs and destroys layers outside it. A fixed-count product (mu-Toni,
    // mu-On) never edits layers after construction and so reads them lock-free by design.
    std::array<std::unique_ptr<Layer>, mu_limits::kMaxLayers> layers;
    std::atomic<int>      numLayers { 0 };
public:
    // True for the whole of a structural layer edit (pointer moves and the parameter moves after
    // them). A product's audio thread skips rendering while it is set, so hosts whose wrapper does
    // not honour suspendProcessing (CLAP) never play a moved layer with half-shifted parameters.
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
    std::unique_ptr<Layer> publishLayerLocked(std::unique_ptr<Layer> fresh)
    {
        const int n = numLayers.load(std::memory_order_relaxed);
        jassert(n < mu_limits::kMaxLayers);
        auto replaced = std::move(layers[(size_t) n]);
        layers[(size_t) n] = std::move(fresh);
        numLayers.store(n + 1, std::memory_order_release);
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
        numLayers.store(n - 1, std::memory_order_release);
        return removed;
    }
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
