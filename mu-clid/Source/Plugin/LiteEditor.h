#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/TransportBar.h"
#include "UI/MasterLoopSection.h"
#include "UI/RhythmCircle.h"
#include "UI/EuclideanPanel.h"
#include "UI/Components/StatusBar.h"
#include "UI/Components/MuLookAndFeel.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/KnobWithLabel.h"
#include "UI/AboutPanel.h"

// Simplified editor for the mu-Clid Lite MIDI-effect build.
// Shows TransportBar + RhythmCircle + EuclideanPanel; no sidebar, voice section, or mixer.
class LiteEditor : public juce::AudioProcessorEditor,
                   public juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit LiteEditor(PluginProcessor&);
    ~LiteEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Host automation arrives on whatever thread setValueNotifyingHost runs on
    // (audio thread under DAW automation); marshal to message thread before
    // touching the dropdown / knob.
    void parameterChanged(const juce::String& parameterID, float newValue) override;

private:
    PluginProcessor& proc;

    MuLookAndFeel lookAndFeel;

    TransportBar      transportBar;
    MasterLoopSection masterLoop;
    RhythmCircle      rhythmCircle;
    EuclideanPanel euclidPanel;
    StatusBar      statusBar;
    AboutPanel     aboutPanel;
    DropdownSelect noteSelector;
    DropdownSelect sizeDropdown;
    juce::Label    noteSelectorLabel;
    KnobWithLabel  accentKnob { "Accent", MuLookAndFeel::knobLevel };

    // Unscaled window dimensions — multiply by mu_ui::s() / mu_ui::scale at use.
    static constexpr int kLiteW        = 1110;
    static constexpr int kLiteH        = 420;
    static constexpr int kTransportH   = 40;
    static constexpr int kStatusH      = 22;
    // Circle uses the family constant; kCircleMargin adds left inset so the
    // circle doesn't sit flush against the window edge.
    static constexpr int kCircleMargin = 12;
    // kControlsH raised to fit Size 1 knobs (kKnobSize1H = 70) with 2 px top/bottom margin.
    static constexpr int kControlsH    = 74;

    void refreshCircle();
    static juce::String midiNoteName(int note);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LiteEditor)
};
