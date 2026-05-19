#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/TransportBar.h"
#include "UI/RhythmSidebar.h"
#include "UI/RhythmPanel.h"
#include "UI/MixerOverlay.h"
#include "UI/AboutPanel.h"
#include "UI/SaveDialog.h"
#include "UI/PresetBrowser.h"
#include "UI/SettingsOverlay.h"
#include "UI/MidiPresetsPanel.h"
#include "UI/Components/StatusBar.h"
#include "UI/Components/MuClidLookAndFeel.h"

class PluginEditor : public juce::AudioProcessorEditor,
                     public juce::KeyListener
{
public:
    explicit PluginEditor(PluginProcessor&);
    ~PluginEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // juce::KeyListener — receives key events in standalone mode.
    bool keyPressed(const juce::KeyPress& key, juce::Component* originator) override;
    bool keyStateChanged(bool isKeyDown, juce::Component* originator) override;

    void parentHierarchyChanged() override;

private:
    PluginProcessor& processorRef;

    MuClidLookAndFeel lookAndFeel;

    TransportBar    transportBar;
    RhythmSidebar   sidebar;
    RhythmPanel     rhythmPanel;
    MixerOverlay    mixerOverlay;
    AboutPanel      aboutPanel;
    SaveDialog      saveDialog;
    PresetBrowser   presetBrowser;
    SettingsOverlay settingsOverlay;
    MidiPresetsPanel midiPresetsPanel;
    StatusBar       statusBar;
    juce::Label     demoBanner;

    static constexpr int kDemoBannerH = 20;

    bool mixerVisible        = false;
    bool aboutVisible        = false;
    bool saveVisible         = false;
    bool browserVisible      = false;
    bool settingsVisible     = false;
    bool midiPresetsVisible  = false;

    juce::ComponentAnimator animator;

    // Fade from→to over durationMs using ComponentAnimator.
    void fadeSwitch(juce::Component* outgoing, juce::Component* incoming, int durationMs = 80);

    void showMixer(bool show);
    void showAbout(bool show);
    void showSaveDialog(bool show);
    void doSavePreset(const juce::String& name, const juce::String& desc,
                      const juce::String& category, bool embedSamples);
    void showPresetBrowser(bool show);
    void showSettings(bool show);
    void showMidiPresets(bool show);

    void hideAllOverlays();

    // consolidates the "refresh chrome after a rhythm-set mutation" boilerplate
    // that was repeated across 4 callbacks (preset load, new preset, sidebar reorder,
    // add rhythm). Each had its own subtly-different combination of refreshItems /
    // setSelectedIndex / setRhythm / mixerOverlay.refresh / mixerOverlay.loadFromAPVTS,
    // and missing any one (especially the mixer reload) was a silent-stale-state bug
    // waiting to happen.
    enum class MixerRefresh { Skip, RefreshOnly, FullReload };
    void selectRhythmAndRefresh(int idx,
                                bool fullSidebarRefresh,
                                MixerRefresh mixerRefresh);

    bool isStandalone    = false;
    bool needsFocusGrab  = false;
    std::function<void()> pendingQuitCallback;
    juce::KeyPress keybindPlayStop { juce::KeyPress::spaceKey };
    void loadKeybindings();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
