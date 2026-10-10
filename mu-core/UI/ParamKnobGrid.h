#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/MuLookAndFeel.h"

#include <memory>
#include <vector>

// Spec-driven wrapping grid of APVTS-bound controls — one rotary knob per float param,
// one combo per choice param, each under a centred name label. A product supplies a list
// of {paramId, label} specs and the grid builds the widgets, attaches them to APVTS, and
// lays them out in a grid that fits as many columns as the width allows, then wraps.
//
// Lifted from mu-on's EnginePanel so any product can build its engine / parameter surface
// from one shared widget (the family-reuse rule). Plugin-agnostic — the product owns the
// {id,label} list; this owns the build + attachment + layout.
namespace mu_ui {

class ParamKnobGrid : public juce::Component
{
public:
    struct Spec { juce::String id; juce::String label; };

    explicit ParamKnobGrid(juce::AudioProcessorValueTreeState& state);

    // Rebuild the controls from `specs` (clears existing + re-attaches). Safe to call
    // repeatedly, e.g. on lane / voice select. Knobs are drawn at the family's standard
    // Size-2 dimensions and wrap to fit the width.
    void setSpecs(const std::vector<Spec>& specs);

    // The width (pixels) the controls need on a single row — a selector takes two cells.
    int getPreferredWidth() const;

    void resized() override;
    void mouseEnter(const juce::MouseEvent&) override;   // a control under the mouse reports its value

    // Fired with the control's label + current value text when a control is hovered or changed.
    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;

    // One row of cells (unscaled): name label over a Size-2 knob, plus a small gap.
    static constexpr int kLabelH = 13;
    static constexpr int kCellH  = kLabelH + MuLookAndFeel::kKnobSize2H + 4;

private:
    struct Control
    {
        juce::String                    label;
        std::unique_ptr<juce::Slider>   knob;
        std::unique_ptr<juce::ComboBox> combo;
        juce::Label                     name;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   knobAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAtt;
    };

    void reportStatus(const Control&) const;

    juce::AudioProcessorValueTreeState&   apvts;
    std::vector<std::unique_ptr<Control>> controls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParamKnobGrid)
};

} // namespace mu_ui
