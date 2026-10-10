#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>     // juce::AudioProcessorPlayer

#include "Link/MuLinkClient.h"
#include "Link/MuLinkPlayHead.h"
#include "Plugin/TimedMidiOut.h"   // mu-core: MIDI out sent when the bus plays it

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

class MuLinkBridge : private juce::Timer, private juce::ChangeListener
{
public:
    // `processor` + `devicePlayer` + `appDeviceManager` are owned by the StandalonePluginHolder and
    // must outlive the bridge. `displayName` is what mu-link shows in its client list.
    // `onConnectionChanged` fires on the message thread when attach/detach happens.
    MuLinkBridge(juce::AudioProcessor& processorToBridge,
                 juce::AudioProcessorPlayer& devicePlayer,
                 juce::AudioDeviceManager& appDeviceManager,
                 juce::String displayName,
                 std::function<void(bool)> onConnectionChangedCb)
        : processor(processorToBridge),
          player(devicePlayer),
          deviceManager(appDeviceManager),
          name(std::move(displayName)),
          onConnectionChanged(std::move(onConnectionChangedCb))
    {
        // Producer-thread render: publish mu-link's (consume-time projected) transport into
        // our playhead, then render the block through the real processor with the app's MIDI in,
        // and queue the MIDI it produces to go out when mu-link plays the block (the bus renders
        // up to one ring ahead of what is heard).
        scratchMidi.ensureSize(2048);   // so MIDI in / an injected program change rarely allocate on the bus thread

        client.onRender([this] (float* const* output, int numChannels, int numFrames,
                                const TransportSnapshot& t)
        {
            playHead.setSnapshot(t);
            juce::AudioBuffer<float> buffer(const_cast<float**>(output), numChannels, numFrames);
            scratchMidi.clear();
            midiIn.removeNextBlockOfMessages(scratchMidi, numFrames);   // the app's enabled MIDI inputs

            // Scene switching: mu-link can target a program change at THIS client (clients share
            // MIDI channels, so it addresses us by slot). Inject it into the processor's MIDI so
            // the product's existing scanMidiProgramChanges picks it up → preset hot-swap. Polled
            // on the producer thread only, so lastPcEpoch needs no synchronisation.
            int pcProgram = 0, pcChannel = 9;
            const bool pcInjected = client.pollProgramChange(pcProgram, pcChannel);
            if (pcInjected)
                scratchMidi.addEvent(juce::MidiMessage::programChange(pcChannel, pcProgram), 0);

            processor.processBlock(buffer, scratchMidi);

            // MIDI out: only a product that makes MIDI (others leave their input in the buffer, which
            // would echo it), never system / real-time bytes (no clock feedback loop through
            // mu-link), and never mu-link's scene change, which was meant for this app.
            if (processor.producesMidi() && midiOut.isActive() && t.sampleRate != 0)
            {
                const double sr = (double) t.sampleRate;
                const double dueStartMs = juce::Time::getMillisecondCounterHiRes()
                                        + client.renderLeadFrames() * 1000.0 / sr;
                for (const auto meta : scratchMidi)
                {
                    if (meta.numBytes < 1 || meta.data[0] >= 0xF0) continue;
                    if (meta.samplePosition == 0 && meta.numBytes == 2 && pcInjected
                        && meta.data[0] == (juce::uint8) (0xC0 | ((pcChannel - 1) & 0x0F))
                        && meta.data[1] == (juce::uint8) (pcProgram & 0x7F))
                        continue;
                    midiOut.push(dueStartMs + meta.samplePosition * 1000.0 / sr, meta.data, meta.numBytes);
                }
            }
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

    // Publish the product's current preset name to mu-link (display-only). Cached so it
    // survives a detach/re-attach (re-pushed in attachToMuLink). Call from the message thread.
    void setPresetName(const juce::String& presetName)
    {
        lastPresetName = presetName;
        if (connected) client.setPresetName(lastPresetName);
    }

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
        // MIDI in follows the bus now: the local player no longer has a processor to feed.
        midiIn.reset((double) snap.sampleRate);
        deviceManager.addMidiInputDeviceCallback({}, &midiIn);
        takeMidiOutPort(deviceManager.getDefaultMidiOutputIdentifier());
        deviceManager.addChangeListener(this);   // follow a MIDI-out change in Settings while attached
        client.start();

        connected  = true;
        lastGen    = client.transportGeneration();
        stallTicks = 0;
        client.setPresetName(lastPresetName);   // re-publish into the freshly-claimed slot
        if (onConnectionChanged) onConnectionChanged(true);
    }

    void detachFromMuLink()
    {
        client.detach();                              // joins the producer thread first
        deviceManager.removeMidiInputDeviceCallback({}, &midiIn);
        deviceManager.removeChangeListener(this);
        returnMidiOutPort();
        processor.setPlayHead(nullptr);               // back to the internal standalone transport
        player.setProcessor(&processor);              // local device drives again (re-prepares)

        connected  = false;
        stallTicks = 0;
        if (onConnectionChanged) onConnectionChanged(false);
    }

    // MIDI out port while attached: the bridge owns it (the family one-owner rule). The app may
    // have no audio device open, and the device manager can replace its port without any callback
    // the bridge would see, so the bridge takes the port from the manager (Windows ports are often
    // single-client), opens its own, and hands it back on detach.
    void takeMidiOutPort(const juce::String& identifier)
    {
        if (identifier.isEmpty()) return;
        outPortId = identifier;
        deviceManager.setDefaultMidiOutputDevice({});   // free the manager's handle first
        outPort = juce::MidiOutput::openDevice(identifier);
        if (processor.producesMidi()) midiOut.start();   // the sender only runs for a MIDI-making app
        midiOut.setOutput(outPort.get());
    }

    // Release the bridge's port: queued notes are dropped, so silence the gear first (notes whose
    // note-off was still queued would hang otherwise).
    void releaseOutPort()
    {
        midiOut.setOutput(nullptr);                   // waits out a send in progress
        if (outPort != nullptr)
            for (int ch = 1; ch <= 16; ++ch)
            {
                outPort->sendMessageNow(juce::MidiMessage::controllerEvent(ch, 64, 0));   // sustain off
                outPort->sendMessageNow(juce::MidiMessage::allNotesOff(ch));
            }
        outPort.reset();
    }

    void returnMidiOutPort()
    {
        releaseOutPort();
        if (outPortId.isNotEmpty())
            deviceManager.setDefaultMidiOutputDevice(outPortId);   // the user's choice, back where it was
        outPortId = {};
    }

    // Settings changed while attached: a newly chosen MIDI output moves to the bridge too. An empty
    // choice is ours (we cleared the manager's port) or the user's "none" — they can't be told
    // apart, so "none" is not honoured while attached; the old choice returns on detach.
    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        if (! connected) return;
        const auto id = deviceManager.getDefaultMidiOutputIdentifier();
        if (id.isEmpty() || id == outPortId) return;
        releaseOutPort();
        takeMidiOutPort(id);
    }

    juce::AudioProcessor&       processor;
    juce::AudioProcessorPlayer& player;
    juce::AudioDeviceManager&   deviceManager;
    juce::String                name;
    juce::String                lastPresetName;   // re-published on each (re)attach
    std::function<void(bool)>   onConnectionChanged;

    juce::MidiMessageCollector midiIn;              // the app's MIDI inputs → the bus render
    mu_core::TimedMidiOut      midiOut { false };   // the bus render's MIDI → the app's MIDI out (started on attach)
    std::unique_ptr<juce::MidiOutput> outPort;      // owned while attached
    juce::String               outPortId;           // the user's MIDI out, handed back on detach
    MuLinkClient   client;
    MuLinkPlayHead playHead;
    juce::MidiBuffer scratchMidi;   // processBlock's MIDI in + out on the bus

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
    MuLinkBridge(juce::AudioProcessor&, juce::AudioProcessorPlayer&, juce::AudioDeviceManager&,
                 juce::String, std::function<void(bool)>) {}
    bool isConnected() const noexcept { return false; }
    void setPresetName(const juce::String&) {}
};

#endif // _WIN32

} // namespace mu_link
