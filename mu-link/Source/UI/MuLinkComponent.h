#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <array>

#include "Server/AudioServer.h"
#include "UI/Components/MuLookAndFeel.h"
#include "UI/Components/VUMeter.h"

// MuLinkComponent — the mu-link window content (Stage L3).
//
// mu-link is a server app, not a plugin, so it does NOT use the plugin EditorShellBase
// (which is a juce::AudioProcessorEditor bound to ProcessorBase). Instead it composes a
// bespoke window from the shared mu-family design system — MuLookAndFeel for the palette
// and VUMeter for the level meters — so it looks like a member of the family without
// pulling in the plugin shell. (Documented deviation per the family-consistency rule.)
//
// Layout: JUCE's AudioDeviceSelectorComponent on the left (the runtime WASAPI/ASIO
// picker + MIDI-clock-out port), and on the right the master transport (play/stop +
// tempo — mu-link is always tempo master) above a mixer-style strip of the eight client
// slots' meters plus the summed master. A timer polls the registry for live attach/
// detach and keeps the MIDI-clock-out port in sync with the picker.
class MuLinkComponent : public juce::Component,
                        private juce::Timer
{
public:
    explicit MuLinkComponent(mu_link::AudioServer& serverToShow);
    ~MuLinkComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void togglePlay();

    mu_link::AudioServer& server;
    MuLookAndFeel         lnf;

    juce::AudioDeviceSelectorComponent deviceSelector;
    juce::Label   titleLabel, subtitleLabel, clientsHeading;
    juce::TextButton playButton { "Stop" };
    juce::Slider  tempoSlider;
    VUMeter       masterMeter;
    juce::Label   masterLabel;
    juce::Slider  masterGain;

    // One mixer strip per client slot: meter + name + gain knob + mute/solo.
    struct ClientStrip
    {
        VUMeter        meter;
        juce::Label    name;
        juce::Slider   gain;
        juce::TextButton mute { "M" }, solo { "S" };
    };
    std::array<ClientStrip, mu_link::kMaxClients> clients;

    bool playing = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MuLinkComponent)
};
