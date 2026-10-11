#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Plugin/HostTransport.h"
#include "Sequencer/Layer.h"

#include <atomic>
#include <cmath>
#include <limits>

// The slim parent of ProcessorBase: the layer accessors and the published host transport.
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
    virtual int         getNumChannels()              const = 0;
    // Layer `i` (name, colour, modulation) or nullptr when `i` is out of range. Message thread only.
    virtual Layer*       getLayer(int i) = 0;
    const Layer*         getLayer(int i) const { return const_cast<ProcessorCore*>(this)->getLayer(i); }
    // The most channels the product can ever have (1..8) — sizes the program-change
    // channel table. Products with a fixed layer count return that count.
    virtual int         getMaxChannels()              const { return 8; }
    virtual juce::String getChannelName(int idx)       const = 0;
    virtual int         getChannelColourIndex(int idx) const = 0;

    // Host transport as last seen by processBlock, for the UI (TransportBar). The UI must never
    // call getPlayHead() itself: the playhead is only valid on the audio thread during a block.
    bool isHostPlaying() const { return hostPlaying.load(std::memory_order_relaxed); }
    // Host meter (4/4 when the host gives none) and current bar start; false = no bar start.
    void getHostTimeSignature(int& numerator, int& denominator) const
    {
        const int packed = hostTimeSig.load(std::memory_order_relaxed);
        numerator   = packed >> 8;
        denominator = packed & 0xFF;
    }
    bool getHostBarStartPpq(double& ppq) const
    {
        const double v = hostBarStart.load(std::memory_order_relaxed);
        if (std::isnan(v)) return false;
        ppq = v;
        return true;
    }
    // Host beat position in quarter notes; false when the host supplied none.
    bool getHostPpqPosition(double& ppq) const
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

protected:
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
