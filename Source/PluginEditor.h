#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/TransportBar.h"
#include "UI/RhythmSidebar.h"
#include "UI/RhythmPanel.h"
#include "UI/MixerOverlay.h"
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

    TransportBar  transportBar;
    RhythmSidebar sidebar;
    RhythmPanel   rhythmPanel;
    MixerOverlay  mixerOverlay;
    StatusBar     statusBar;

    bool mixerVisible = false;
    void showMixer(bool show);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
