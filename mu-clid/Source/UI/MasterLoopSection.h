#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Plugin/PluginProcessor.h"
#include "UI/Components/DropdownSelect.h"

// mu-clid's transport-bar "Master Loop" sub-pane — picks the master-loop length
// (Off / 16..256 steps) and displays the current step counter while playing.
// Mounted into the shared mu-core TransportBar via setLoopSection() so the
// shell layout stays the same as before the shell-lift refactor.
class MasterLoopSection : public juce::Component,
                          public juce::AudioProcessorValueTreeState::Listener,
                          private juce::Timer
{
public:
    explicit MasterLoopSection(PluginProcessor& proc);
    ~MasterLoopSection() override;

    // Width (in baseline pixels, pre-scale) the section needs for its three
    // controls + paddings. Pass to TransportBar::setLoopSection().
    static constexpr int kWidth = 6 /*pad*/ + 36 /*label*/ + 2 + 100 /*dropdown*/ + 2 + 56 /*step counter*/ + 6 /*pad*/;

    // Status-bar wiring — fired when the user picks a new loop length.
    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;

    void resized() override;

private:
    PluginProcessor& proc;

    juce::Label    loopLabel;
    DropdownSelect loopDropdown;
    juce::Label    loopStepLabel;

    void timerCallback() override;
    void syncLoopDropdownFromAPVTS();
    void parameterChanged(const juce::String& parameterID, float newValue) override;

    // Last values shown in loopStepLabel — the timer only rebuilds the string when
    // one changes, so a 30 Hz tick doesn't allocate a juce::String every frame.
    int lastShownStep = -1;
    int lastShownSteps = -1;
};
