#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <functional>

#include "Server/AudioServer.h"
#include "UI/ShellChrome.h"
#include "UI/Components/SegmentControl.h"
#include "LinkShellBackend.h"
#include "MixerPanel.h"
#include "ScenesPanel.h"
#include "HoverStatus.h"

// MuLinkComponent - the mu-link window content (docs/mu-link/design-ui.md).
//
// mu-link is a server app, not a plugin, so it has no AudioProcessor and no EditorShellBase. It uses
// the same chrome as the products through the app seam: a mu_ui::ShellChrome over a
// mu_link::LinkShellBackend gives it the shared transport bar, status bar, About, Save and preset
// browser overlays and the family look. The main area is the Mixer panel (eight client strips and the
// master column) above the Scenes panel; there is no sidebar. The window owns the app state that a
// preset captures (mixer and scenes) and hands it to the backend through two hooks.
class MuLinkComponent : public juce::Component
{
public:
    // `dataRoot` is where mu-link keeps its files (Presets, settings, scenes.json); the default is
    // Documents/TDP/muLink.
    explicit MuLinkComponent(mu_link::AudioServer& serverToShow,
                             juce::File dataRoot = mu_link::LinkShellBackend::defaultRoot());
    ~MuLinkComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // The parts, for the window's owner and the tests.
    mu_ui::ShellChrome&       getChrome()  { return chrome; }
    mu_link::LinkShellBackend& getBackend() { return backend; }
    mu_link::MixerPanel&      getMixer()   { return page.mixer; }
    mu_link::ScenesPanel&     getScenes()  { return page.scenes; }
    SegmentControl&           getClockSourceControl() { return clockSource.segment; }

    // Recall scene `scene`: each enabled, connected client is sent its program change.
    void triggerScene(int scene);

    // The whole app state as one tree (tempo, mixer, master inserts, scenes), and back.
    juce::ValueTree captureState() const;
    void            applyState(const juce::ValueTree& state);
    void            resetToDefaults();   // 120.0 BPM, unity gains, flat EQ, inserts None, scenes cleared

private:
    // The clock source switch that sits in the transport bar's loop-section slot.
    struct ClockSourceSection : juce::Component
    {
        ClockSourceSection();
        void resized() override;
        SegmentControl segment { { "Internal", "MIDI In" } };
    };

    // The main area: the Mixer panel above the Scenes panel.
    struct MainPage : juce::Component
    {
        explicit MainPage(mu_link::AudioServer& server) : mixer(server)
        {
            addAndMakeVisible(mixer);
            addAndMakeVisible(scenes);
        }
        void resized() override;
        mu_link::MixerPanel  mixer;
        mu_link::ScenesPanel scenes;
    };

    void wireTransportBar();
    void wirePresets();
    void showClockSourceStatus();
    juce::String clockSourceText() const;

    juce::File scenesFile() const;
    void       loadScenes();
    void       saveScenes() const;

    void doSavePreset(const juce::String& name, const juce::String& desc, const juce::String& category);
    void loadPresetFile(const juce::File& file);

    mu_link::AudioServer&     server;
    mu_link::LinkShellBackend backend;
    mu_ui::ShellChrome        chrome;   // declared first among the children: destroyed last

    ClockSourceSection clockSource;
    MainPage           page;
    mu_link::HoverStatus hover;   // after the components it watches

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MuLinkComponent)
};
