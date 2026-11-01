#pragma once

#include "Sequencer/SliceSettings.h"
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/MuLookAndFeel.h"
#include "UI/Components/SegmentControl.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

// The slice player panel (docs/mu-tilate/design-ui.md, "Slice player"): the settings of one slice as knobs and switches, in
// groups: slice (direction, level, pan, fades), pitch, pre-fade, filter, amp envelope, filter envelope and loop. It shows the
// settings it is given and reports every change as a whole SliceSettings; it edits no data itself.
namespace mu_tilate
{

class SlicePanel : public juce::Component
{
public:
    SlicePanel();

    // The slice to edit: its index (-1 for none), its settings and a line describing it ("Slice 5 of 56, 0.126 s").
    void setSlice(int index, const SliceSettings& settings, const juce::String& description);
    int  getSlice() const noexcept { return slice; }

    // The user changed a setting of the shown slice.
    std::function<void(const SliceSettings&)> onChange;
    // The user asked for the previous (-1) or next (+1) slice.
    std::function<void(int step)> onStep;
    // The status bar's name and value for the control being changed.
    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        std::unique_ptr<KnobWithLabel> comp;
        float SliceSettings::* field;
        int group;
    };
    enum Group { Slice, Pitch, Pre, Filter, AmpEnv, FilterEnv };

    void addKnob(Group group, const char* label, float SliceSettings::* field, double lo, double hi, double step, double skewMid,
                 MuLookAndFeel::ColourIds colour, const juce::String& statusName, std::function<juce::String(double)> format);
    void addSwitch(SegmentControl& control, Group group);
    void changed();   // reads every control into `settings` and reports it

    int slice = -1;
    SliceSettings settings;
    bool updating = false;   // setSlice is writing the controls: they must not report back
    juce::String description;

    juce::TextButton prevButton { "<" }, nextButton { ">" };
    SegmentControl reverseSwitch { { "Fwd", "Rev" },  SegmentControl::ActiveStyle::General, SegmentControl::DrawStyle::Lcd };
    SegmentControl preSwitch     { { "Off", "On" },   SegmentControl::ActiveStyle::General, SegmentControl::DrawStyle::Lcd };
    SegmentControl filterSwitch  { { "Off", "LP", "HP", "BP" }, SegmentControl::ActiveStyle::General, SegmentControl::DrawStyle::Lcd };
    SegmentControl loopSwitch    { { "Once", "Loop" }, SegmentControl::ActiveStyle::General, SegmentControl::DrawStyle::Lcd };
    std::vector<Knob> knobs;
    std::vector<std::pair<SegmentControl*, int>> switches;   // each switch and its group
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> titles;   // each group's title and where resized() put it

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SlicePanel)
};

} // namespace mu_tilate
