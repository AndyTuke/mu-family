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
    juce::TextButton playButton { "Play" };
    juce::TextButton clockSourceButton { "Clock: Internal" };
    juce::Slider  tempoSlider;
    VUMeter       masterMeter;
    juce::Label   masterLabel;
    juce::Slider  masterGain;

    // One mixer strip per client slot: meter + name + gain knob + mute/solo, plus the
    // selected scene's per-client cell editor (enable + program + channel).
    struct ClientStrip
    {
        VUMeter        meter;
        juce::Label    name;
        juce::Slider   gain;
        juce::TextButton mute { "M" }, solo { "S" };
        juce::ToggleButton sceneOn;        // is this client targeted by the selected scene?
        juce::Label        scenePc;        // editable program number 0-127
        juce::Label        sceneCh;        // editable MIDI channel 1-16 (9 = full preset)
    };
    std::array<ClientStrip, mu_link::kMaxClients> clients;

    // ── Scenes ───────────────────────────────────────────────────────────────
    // A scene is a set of per-client program changes (each with its own MIDI channel, so a
    // scene can mix full-preset recalls (Ch 9) with per-layer swaps (Ch 1-8)). Triggering a
    // scene sends each enabled client its program change via mu_link::sendProgramChange.
    static constexpr int kNumScenes = 8;
    struct SceneCell { bool enabled = false; int program = 0; int channel = 9; };
    std::array<std::array<SceneCell, mu_link::kMaxClients>, kNumScenes> scenes {};
    int editScene = 0;   // which scene the per-client cell editors are bound to

    juce::Label                                  scenesHeading;
    std::array<juce::TextButton, kNumScenes>     sceneButtons;

    void triggerScene(int s);          // send each enabled client its program change
    void selectScene(int s);           // bind the cell editors to scene s (and highlight it)
    void refreshSceneEditors();        // reload the editors from scenes[editScene]
    juce::File scenesFile() const;     // Documents/TDP/muLink/scenes.json
    void loadScenes();
    void saveScenes() const;

    bool playing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MuLinkComponent)
};
