#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <array>

#include "Server/AudioServer.h"
#include "UI/Components/MuLookAndFeel.h"
#include "UI/Components/VUMeter.h"
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/SaveDialog.h"
#include "UI/PresetBrowser.h"

// MuLinkComponent — the mu-link window content (Stage L3).
//
// mu-link is a server app, not a plugin, so it does NOT use the plugin EditorShellBase
// (which is a juce::AudioProcessorEditor bound to ProcessorBase). Instead it composes a
// bespoke window from the shared mu-family design system — MuLookAndFeel for the palette
// and VUMeter for the level meters — so it looks like a member of the family without
// pulling in the plugin shell. (Documented deviation per the family-consistency rule.)
//
// Layout: the master transport (play/stop + tempo — mu-link is always tempo master) above a
// full-width mixer strip of the eight client slots (fader + VU + 3-band EQ) plus the summed
// master. The runtime WASAPI/ASIO picker + MIDI-clock-out port lives behind an Options button
// (a dialog, like the synth standalones). A timer polls the registry for live attach/detach.
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
    void showOptions();        // opens the audio/MIDI device picker in a dialog

    // ── Presets — full app state (mixer + scenes) saved as a .muLink XML preset ──
    // Reuses the shared SaveDialog + PresetBrowser overlays for identical layout/display to
    // the products. A preset captures everything: tempo, per-client gain/mute/solo + EQ,
    // master gain, both master inserts, and all 8 scenes.
    juce::ValueTree captureState() const;          // current UI/server/scene state → tree
    void            applyState(const juce::ValueTree& state);   // tree → UI/server/scenes
    juce::File      presetsDir() const;            // Documents/TDP/muLink/Presets
    void            doSavePreset(const juce::String& name, const juce::String& desc,
                                 const juce::String& category);
    void            loadPresetFile(const juce::File& file);
    void            showSaveDialog(bool show);
    void            showPresetBrowser(bool show);

    mu_link::AudioServer& server;
    MuLookAndFeel         lnf;

    juce::Label   titleLabel, subtitleLabel, clientsHeading;
    juce::TextButton optionsButton { "Options" };
    juce::TextButton labelModeButton { "Show: Names" };   // toggles strip labels Names ⇄ Presets
    bool showPresetNames = false;
    juce::TextButton playButton { "Play" };
    juce::TextButton clockSourceButton { "Clock: Internal" };
    juce::TextButton saveButton { "Save" };
    juce::TextButton browseButton { "Presets" };
    juce::Label   presetNameLabel;        // shows the currently-loaded preset name
    juce::Slider  tempoSlider;
    VUMeter       masterMeter;
    juce::Label   masterLabel;
    juce::Slider  masterGain;

    // Two master-bus insert effects (like mu-clid / mu-tant): an algorithm dropdown + four
    // self-relabelling slot knobs (driven by mu_ui::configureKnobFromSlot from the shared
    // per-algo config table), wired to the server's master-insert atomics.
    struct MasterInsert
    {
        juce::Label    title;
        DropdownSelect algo;
        KnobWithLabel  p[4] { { "P1", MuLookAndFeel::knobInsertPad },
                              { "P2", MuLookAndFeel::knobInsertPad },
                              { "P3", MuLookAndFeel::knobInsertPad },
                              { "P4", MuLookAndFeel::knobInsertPad } };
    };
    std::array<MasterInsert, 2> masterIns;
    void configureMasterInsert(int which);   // (re)bind the 4 knobs to the selected algorithm

    // One mixer strip per client slot: meter + name + gain knob + mute/solo, plus the
    // selected scene's per-client cell editor (enable + program + channel).
    struct ClientStrip
    {
        VUMeter        meter;
        juce::Label    name;
        juce::Slider   gain;
        juce::Slider   eq[4];          // 3-band EQ insert: Low / Mid / Mid-Hz / High (vertical)
        juce::Label    eqLabel[4];
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

    // Preset overlays (shared mu-core components → identical layout to the products).
    SaveDialog    saveDialog;
    PresetBrowser presetBrowser;
    juce::String  currentPresetName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MuLinkComponent)
};
