#include "MuLinkBridge.h"

namespace mu_clid
{

MuLinkBridge::MuLinkBridge(juce::AudioProcessor& proc,
                           juce::AudioProcessorPlayer& devicePlayer,
                           std::function<void(bool)> onConnectionChangedCb)
    : processor(proc), player(devicePlayer), onConnectionChanged(std::move(onConnectionChangedCb))
{
    // Producer-thread render: publish mu-link's (consume-time projected) transport into our
    // playhead, then render the block through the real processor. Because the processor
    // reads play state + position via getPlayHead(), the sequencer follows mu-link with no
    // processBlock change — the same path a DAW host drives. MIDI out is discarded here.
    client.onRender([this] (float* const* output, int numChannels, int numFrames,
                            const mu_link::TransportSnapshot& t)
    {
        playHead.setSnapshot(t);
        juce::AudioBuffer<float> buffer(const_cast<float**>(output), numChannels, numFrames);
        scratchMidi.clear();
        processor.processBlock(buffer, scratchMidi);
    });

    startTimer(500);   // poll for mu-link appearing / disappearing
}

MuLinkBridge::~MuLinkBridge()
{
    stopTimer();
    if (connected)
        detachFromMuLink();
}

void MuLinkBridge::timerCallback()
{
    if (! connected)
    {
        attachToMuLink();
        return;
    }

    // mu-link's transport generation advances every server block (even while stopped). A
    // frozen generation across polls means mu-link has quit or closed its device → fall back.
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

void MuLinkBridge::attachToMuLink()
{
    // Claim a slot but DON'T start rendering yet — we must re-point the processor first so
    // it is never driven by the local device and the bus thread at the same time.
    if (! client.attach("mu-Clid", mu_link::kMaxChannels, /*startRendering*/ false))
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

void MuLinkBridge::detachFromMuLink()
{
    client.detach();                              // joins the producer thread first
    processor.setPlayHead(nullptr);               // back to the internal standalone transport
    player.setProcessor(&processor);              // local device drives again (re-prepares)

    connected  = false;
    stallTicks = 0;
    if (onConnectionChanged) onConnectionChanged(false);
}

} // namespace mu_clid
