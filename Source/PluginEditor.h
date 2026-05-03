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
#include "UI/Components/StatusBar.h"
#include "UI/Components/MuClidLookAndFeel.h"

class PluginEditor : public juce::AudioProcessorEditor
{
public:
    explicit PluginEditor(PluginProcessor&);
    ~PluginEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

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
    StatusBar       statusBar;

    bool mixerVisible    = false;
    bool aboutVisible    = false;
    bool saveVisible     = false;
    bool browserVisible  = false;
    bool settingsVisible = false;

    void showMixer(bool show);
    void showAbout(bool show);
    void showSaveDialog(bool show);
    void showPresetBrowser(bool show);
    void showSettings(bool show);

    void hideAllOverlays();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
