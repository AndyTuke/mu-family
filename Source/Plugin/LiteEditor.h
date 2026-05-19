#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/TransportBar.h"
#include "UI/RhythmCircle.h"
#include "UI/EuclideanPanel.h"
#include "UI/Components/StatusBar.h"
#include "UI/Components/MuClidLookAndFeel.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/KnobWithLabel.h"
#include "UI/AboutPanel.h"

// Simplified editor for the mu-Clid Lite MIDI-effect build.
// Shows TransportBar + RhythmCircle + EuclideanPanel; no sidebar, voice section, or mixer.
class LiteEditor : public juce::AudioProcessorEditor
{
public:
    explicit LiteEditor(PluginProcessor&);
    ~LiteEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    PluginProcessor& proc;

    MuClidLookAndFeel lookAndFeel;

    TransportBar   transportBar;
    RhythmCircle   rhythmCircle;
    EuclideanPanel euclidPanel;
    StatusBar      statusBar;
    AboutPanel     aboutPanel;
    DropdownSelect noteSelector;
    juce::Label    noteSelectorLabel;
    KnobWithLabel  accentKnob { "Accent", MuClidLookAndFeel::knobLevel };

    static constexpr int kTransportH  = 40;
    static constexpr int kStatusH     = 22;
    static constexpr int kCircleSize  = 300;
    static constexpr int kControlsH   = 60;

    void refreshCircle();
    static juce::String midiNoteName(int note);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LiteEditor)
};
