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
            ctl->combo->onChange = [this, c = ctl.get()] { reportStatus(*c); };
            ctl->combo->addMouseListener(this, false);
        }
        else
        {
            // The standard knob with an empty label (the grid's own name label sits above it), so the
            // dial, value text and modulation rings are the family's; its bounds are set in resized().
            ctl->knob = std::make_unique<KnobWithLabel>(juce::String());
            addAndMakeVisible(*ctl->knob);
            ctl->knobAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                apvts, spec.id, ctl->knob->getSlider());
            ctl->knob->onStatusUpdate = [this, c = ctl.get()](const juce::String&, const juce::String&) { reportStatus(*c); };
            ctl->knob->onValueChanged = [this, c = ctl.get()](double) { reportStatus(*c); };
            if (spec.tooltip.isNotEmpty())
                ctl->knob->getSlider().setTooltip(spec.tooltip);
            if (spec.modDestId != nullptr)
                ctl->knob->bindModulation(spec.modDestId, spec.modMatrix, spec.modLive);
        }
        controls.push_back(std::move(ctl));
    }
    resized();
}

void ParamKnobGrid::clearModBindings()
{
    for (auto& ctl : controls)
        if (ctl->knob != nullptr) ctl->knob->clearModBinding();
}

int ParamKnobGrid::getPreferredWidth() const
{
    const int cellW = mu_ui::s(MuLookAndFeel::kKnobSize2W) + mu_ui::s(6);
    int w = 0;
    for (const auto& ctl : controls)
        w += ctl->combo ? 2 * cellW : cellW;
    return w;
}

void ParamKnobGrid::resized()
{
    using mu_ui::s;
    using mu_ui::sf;
    auto area = getLocalBounds();
    if (controls.empty()) return;

    // Knobs are drawn at one of the family's standard sizes, not stretched to the cell. Size 2 (the per-channel
    // default used across mu-clid/mu-tant) unless the controls would not fit the width on one row there and
    // would on Size 3, the next size down. The grid then fits as many standard cells per row as the width allows
    // and wraps.
    const int labelH = s(kLabelH);
    auto rowWidthAt = [&](int knobCellW) { int w = 0; for (const auto& c : controls) w += c->combo ? 2 * (knobCellW + s(6)) : knobCellW + s(6); return w; };
    const bool smaller = rowWidthAt(s(MuLookAndFeel::kKnobSize2W)) > area.getWidth()
                         && rowWidthAt(s(MuLookAndFeel::kKnobSize3W)) <= area.getWidth();
    const int knobW  = s(smaller ? MuLookAndFeel::kKnobSize3W : MuLookAndFeel::kKnobSize2W);
    const int knobH  = s(smaller ? MuLookAndFeel::kKnobSize3H : MuLookAndFeel::kKnobSize2H);
    const int comboH = s(24);
    const int cellW  = knobW + s(6);
    const int cellH  = labelH + knobH + s(4);

    // Flow the cells left to right, wrapping at the width. A selector takes two cells so its
    // text fits (the metal style's LCD lettering is wider than one knob).
    int x = area.getX(), y = area.getY();
    for (auto& ctl : controls)
    {
        const int cw = ctl->combo ? 2 * cellW : cellW;
        if (x > area.getX() && x + cw > area.getRight()) { x = area.getX(); y += cellH; }
        juce::Rectangle<int> cell(x, y, cw, cellH);
        x += cw;
        ctl->name.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(MuLookAndFeel::kKnobLabelFont))));   // the knobs' own label size
        ctl->name.setBounds(cell.removeFromTop(labelH));
        // Centre the standard-size control in the remaining cell.
        if (ctl->combo)
            ctl->combo->setBounds(cell.withSizeKeepingCentre(cw - 2 * s(MuLookAndFeel::kDropdownEdgeGap), comboH));
        else if (ctl->knob)
        {
            // The dial itself keeps the standard knob size; the knob component adds the band above
            // and the (empty) label band below that it reserves around the dial.
            const auto dial = cell.withSizeKeepingCentre(knobW, knobH);
            ctl->knob->setBounds(dial.withTrimmedTop(-s(MuLookAndFeel::kKnobTopPad))
                                     .withTrimmedBottom(-s(MuLookAndFeel::kKnobLabelH)));
        }
    }
}

void ParamKnobGrid::mouseEnter(const juce::MouseEvent& e)
{
    for (const auto& ctl : controls)
        if (e.eventComponent == ctl->combo.get())
            reportStatus(*ctl);
}

void ParamKnobGrid::reportStatus(const Control& ctl) const
{
    if (! onStatusUpdate) return;
    if (ctl.knob)       onStatusUpdate(ctl.label, ctl.knob->getSlider().getTextFromValue(ctl.knob->getValue()));
    else if (ctl.combo) onStatusUpdate(ctl.label, ctl.combo->getText());
}

} // namespace mu_ui
