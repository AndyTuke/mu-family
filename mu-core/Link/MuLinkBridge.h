#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>     // juce::AudioProcessorPlayer

#include "Link/MuLinkClient.h"
#include "Link/MuLinkPlayHead.h"

#include <functional>

// MuLinkBridge — STANDALONE-ONLY glue between a mu-family standalone and a running mu-link.
// Shared by every product (lives in mu-core/Link).
//
// **Header-only on purpose.** It is NOT added to mu-core's INTERFACE source list, so it is
// compiled *only* into the translation unit that includes it — each product's StandaloneApp.
// The VST3/CLAP builds never include it, so a plugin can never attach to mu-link (the host
// owns its clock + device). Family rule: standalone-only, see docs/mu-link/design-mulink.md §3.3.
//
// While running it polls for mu-link. When present (with an audio device), it hands the
// processor to the bus: detaches it from the local AudioProcessorPlayer so the machine's own
// device stops driving it, slaves its transport via MuLinkPlayHead, and lets MuLinkClient's
// render-ahead thread drive processBlock at mu-link's sample rate. When mu-link quits (its
// transport generation freezes) it hands the processor back to the local device — seamless
// fallback. A product with no mu-link present behaves exactly as before.
//
// The product reads play state + position via getPlayHead() (the mu-core HostTransport
// standard), so slaving needs no processBlock change beyond consulting the playhead in
// standalone mode.
namespace mu_link
{

#ifdef _WIN32

class MuLinkBridge : private juce::Timer
{
public:
    // `processor` + `devicePlayer` are owned by the StandalonePluginHolder and must outlive
    // the bridge. `displayName` is what mu-link shows in its client list. `onConnectionChanged`
    // fires on the message thread when attach/detach happens.
    MuLinkBridge(juce::AudioProcessor& processorToBridge,
                 juce::AudioProcessorPlayer& devicePlayer,
                 juce::String displayName,
                 std::function<void(bool)> onConnectionChangedCb)
        : processor(processorToBridge),
          player(devicePlayer),
          name(std::move(displayName)),
          onConnectionChanged(std::move(onConnectionChangedCb))
    {
        // Producer-thread render: publish mu-link's (consume-time projected) transport into
        // our playhead, then render the block through the real processor. MIDI out discarded.
        client.onRender([this] (float* const* output, int numChannels, int numFrames,
                                const TransportSnapshot& t)
        {
            playHead.setSnapshot(t);
            juce::AudioBuffer<float> buffer(const_cast<float**>(output), numChannels, numFrames);
            scratchMidi.clear();
            processor.processBlock(buffer, scratchMidi);
        });

        startTimer(500);   // poll for mu-link appearing / disappearing
    }

    ~MuLinkBridge() override
    {
        stopTimer();
        if (connected)
            detachFromMuLink();
    }

    bool isConnected() const noexcept { return connected; }

private:
    void timerCallback() override
    {
        if (! connected)
        {
            attachToMuLink();
            return;
        }

        // mu-link's transport generation advances every server block (even while stopped). A
        // frozen generation across polls means mu-link quit/closed its device → fall back.
        const auto gen = client.transportGeneration();
        if (gen == lastGen)
        {
            if (++stallTicks >= 2)
                detachFromMuLink();
        }
        else
        {
            lastGen    = gen;
            stallTicks = 0;
        }
    }

    void attachToMuLink()
    {
        // Claim a slot but DON'T start rendering yet — we must re-point the processor first so
        // it is never driven by the local device and the bus thread at the same time.
        if (! client.attach(name, kMaxChannels, /*startRendering*/ false))
            return;                                   // mu-link not running / incompatible / full

        const auto snap = client.transport();
        if (snap.sampleRate == 0)
        {
            client.detach();                          // mu-link is up but has no audio device yet
            return;                                   // try again on the next poll
        }

        // Hand the processor to the bus: stop the local device driving it, slave its transport,
        // re-prepare at mu-link's sample rate, then start the producer thread.
        player.setProcessor(nullptr);
        processor.setPlayHead(&playHead);
        processor.setRateAndBufferSizeDetails((double) snap.sampleRate, kRenderBlockMax);
        processor.prepareToPlay((double) snap.sampleRate, kRenderBlockMax);
        client.start();

        connected  = true;
        lastGen    = client.transportGeneration();
        stallTicks = 0;
        if (onConnectionChanged) onConnectionChanged(true);
    }

    void detachFromMuLink()
    {
        client.detach();                              // joins the producer thread first
        processor.setPlayHead(nullptr);               // back to the internal standalone transport
        player.setProcessor(&processor);              // local device drives again (re-prepares)

        connected  = false;
        stallTicks = 0;
        if (onConnectionChanged) onConnectionChanged(false);
    }

    juce::AudioProcessor&       processor;
    juce::AudioProcessorPlayer& player;
    juce::String                name;
    std::function<void(bool)>   onConnectionChanged;

    MuLinkClient   client;
    MuLinkPlayHead playHead;
    juce::MidiBuffer scratchMidi;   // processBlock's MIDI out, discarded on the bus

    bool          connected  = false;
    std::uint64_t lastGen    = 0;
    int           stallTicks = 0;

    static constexpr int kRenderBlockMax = 512;   // matches MuLinkClient's render chunk

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MuLinkBridge)
};

#else // _WIN32

// mu-link's shared-memory bus is currently Windows-only (a POSIX shm_open/mmap backend is
// the pending cross-platform port). Off Windows the bridge is a no-op with the same public
// shape, so each product's StandaloneApp compiles and runs unchanged — it simply behaves as
// if mu-link were never present (own device, internal transport).
class MuLinkBridge
{
public:
    MuLinkBridge(juce::AudioProcessor&, juce::AudioProcessorPlayer&,
                 juce::String, std::function<void(bool)>) {}
    bool isConnected() const noexcept { return false; }
};

#endif // _WIN32

} // namespace mu_link
