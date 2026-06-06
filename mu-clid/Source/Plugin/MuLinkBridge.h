#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>     // juce::AudioProcessorPlayer

#include "Link/MuLinkClient.h"
#include "MuLinkPlayHead.h"

#include <functional>

// MuLinkBridge — STANDALONE-ONLY glue between mu-Clid and a running mu-link (Stage L5).
//
// Compiled only into mu-clid_Standalone (alongside StandaloneApp.cpp); the VST3/CLAP
// builds never see it, so a plugin can never attach to mu-link — the host owns its clock
// and device. (Family rule: standalone-only, see docs/mu-link/design-mulink.md §3.3.)
//
// While running it polls for mu-link. When present (and it has an audio device), it hands
// the processor over to the bus: detaches it from the local AudioProcessorPlayer so the
// machine's own device stops driving it, slaves its transport via MuLinkPlayHead, and lets
// MuLinkClient's render-ahead thread drive processBlock at mu-link's sample rate. When
// mu-link quits (its transport generation freezes), it hands the processor back to the
// local device — seamless fallback. mu-clid with no mu-link present behaves exactly as
// before.
namespace mu_clid
{

class MuLinkBridge : private juce::Timer
{
public:
    // `processor` + `devicePlayer` are owned by the StandalonePluginHolder and must
    // outlive the bridge. `onConnectionChanged(bool)` fires on the message thread.
    MuLinkBridge(juce::AudioProcessor& processor,
                 juce::AudioProcessorPlayer& devicePlayer,
                 std::function<void(bool)> onConnectionChanged);
    ~MuLinkBridge() override;

    bool isConnected() const noexcept { return connected; }

private:
    void timerCallback() override;
    void attachToMuLink();
    void detachFromMuLink();

    juce::AudioProcessor&       processor;
    juce::AudioProcessorPlayer& player;
    std::function<void(bool)>   onConnectionChanged;

    mu_link::MuLinkClient client;
    MuLinkPlayHead        playHead;
    juce::MidiBuffer      scratchMidi;   // processBlock's MIDI out, discarded on the bus

    bool          connected  = false;
    std::uint64_t lastGen    = 0;
    int           stallTicks = 0;

    static constexpr int kRenderBlockMax = 512;   // matches MuLinkClient's render chunk

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MuLinkBridge)
};

} // namespace mu_clid
