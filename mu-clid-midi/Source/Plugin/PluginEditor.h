#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Plugin/PluginProcessor.h"
#include "UI/TransportBar.h"
#include "UI/MasterLoopSection.h"
#include "UI/RhythmCircle.h"      // mu-core
#include "UI/EuclideanPanel.h"    // mu-core
#include "UI/Components/StatusBar.h"
#include "UI/Components/MuLookAndFeel.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/KnobWithLabel.h"
#include "UI/AboutOverlay.h"

namespace mu_clid_midi {

// The mu-Clid MIDI editor: TransportBar + RhythmCircle + EuclideanPanel, with the MIDI note and
// accent controls. No sidebar, voice section or mixer.
class PluginEditor : public juce::AudioProcessorEditor,
                     public juce::AudioProcessorValueTreeState::Listener,
                     private juce::Timer
{
public:
    explicit PluginEditor(PluginProcessor&);
    ~PluginEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Host automation arrives on whatever thread setValueNotifyingHost runs on (the audio thread under DAW
    // automation), so this only raises a flag: no allocation, no UI work. timerCallback reads the parameter
    // and updates the dropdown / knob on the message thread.
    void parameterChanged(const juce::String& parameterID, float newValue) override;

private:
    void timerCallback() override;
    std::atomic<bool> noteDirty   { false };
    std::atomic<bool> accentDirty { false };

    PluginProcessor& proc;

    MuLookAndFeel lookAndFeel;

    TransportBar        transportBar;
    MasterLoopSection   masterLoop;
    mu_ui::RhythmCircle    rhythmCircle;
    mu_ui::EuclideanPanel  euclidPanel;
    StatusBar      statusBar;
    AboutOverlay     aboutOverlay;
    DropdownSelect noteSelector;
    DropdownSelect sizeDropdown;
    juce::Label    noteSelectorLabel;
    juce::Label    logicLabel;   // the Logic dropdown itself is euclidPanel's
    KnobWithLabel  accentKnob { "Accent", MuLookAndFeel::knobLevel };

    // Unscaled window dimensions — multiply by mu_ui::s() / mu_ui::scale at use.
    static constexpr int kWindowW      = 1110;
    static constexpr int kWindowH      = 420;
    static constexpr int kTransportH   = 40;
    static constexpr int kStatusH      = 22;
    // Circle uses the family constant; kCircleMargin adds left inset so the
    // circle doesn't sit flush against the window edge.
    static constexpr int kCircleMargin = MuLookAndFeel::kSpaceL;
    // kControlsH raised to fit Size 1 knobs (kKnobSize1H = 70) with 2 px top/bottom margin.
    static constexpr int kControlsH    = 74;

    void refreshCircle();
    static juce::String midiNoteName(int note);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

} // namespace mu_clid_midi
