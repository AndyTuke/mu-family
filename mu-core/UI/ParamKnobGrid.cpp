#include "ParamKnobGrid.h"
#include "UI/Components/MuLookAndFeel.h"   // standard knob sizes + UI scaling

namespace mu_ui {

ParamKnobGrid::ParamKnobGrid(juce::AudioProcessorValueTreeState& state) : apvts(state) {}

void ParamKnobGrid::setSpecs(const std::vector<Spec>& specs)
{
    controls.clear();

    for (const auto& spec : specs)
    {
        auto ctl = std::make_unique<Control>();
        ctl->label = spec.label;

        ctl->name.setText(spec.label, juce::dontSendNotification);
        ctl->name.setJustificationType(juce::Justification::centred);
        ctl->name.setFont(juce::Font(juce::FontOptions(11.0f)));
        addAndMakeVisible(ctl->name);

        // Choice params get a combo box; everything else a rotary knob.
        if (dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(spec.id)) != nullptr)
        {
            ctl->combo = std::make_unique<juce::ComboBox>();
            if (auto* cp = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(spec.id)))
                ctl->combo->addItemList(cp->choices, 1);
            addAndMakeVisible(*ctl->combo);
            ctl->comboAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
                apvts, spec.id, *ctl->combo);
        }
        else
        {
            ctl->knob = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                                       juce::Slider::NoTextBox);
            addAndMakeVisible(*ctl->knob);
            ctl->knobAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                apvts, spec.id, *ctl->knob);
        }
        controls.push_back(std::move(ctl));
    }
    resized();
}

void ParamKnobGrid::resized()
{
    using mu_ui::s;
    auto area = getLocalBounds();
    if (controls.empty()) return;

    // Knobs are drawn at one of the family's standard sizes (Size 2 — the per-channel
    // default used across mu-clid/mu-tant), not stretched to the cell. The grid fits as many
    // standard cells per row as the width allows, then wraps.
    const int labelH = s(13);
    const int knobW  = s(MuLookAndFeel::kKnobSize2W);
    const int knobH  = s(MuLookAndFeel::kKnobSize2H);
    const int comboH = s(24);
    const int cellW  = knobW + s(6);
    const int cellH  = labelH + knobH + s(4);

    const int n      = (int) controls.size();
    const int perRow = juce::jlimit(1, n, juce::jmax(1, area.getWidth() / cellW));

    for (int i = 0; i < n; ++i)
    {
        const int r = i / perRow, c = i % perRow;
        juce::Rectangle<int> cell(area.getX() + c * cellW, area.getY() + r * cellH, cellW, cellH);
        controls[(size_t) i]->name.setBounds(cell.removeFromTop(labelH));
        // Centre the standard-size control in the remaining cell.
        if (controls[(size_t) i]->combo)
            controls[(size_t) i]->combo->setBounds(cell.withSizeKeepingCentre(knobW, comboH));
        else if (controls[(size_t) i]->knob)
            controls[(size_t) i]->knob->setBounds(cell.withSizeKeepingCentre(knobW, knobH));
    }
}

} // namespace mu_ui
