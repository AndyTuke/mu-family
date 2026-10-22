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
        ctl->newRow = spec.newRow;
        ctl->groupStart = spec.groupStart;

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
    // default used across mu-clid/mu-tant) unless the controls would not fit the area (its rows, wrapped at the width) there, then
    // the next standard size down that fits. The grid then fits as many standard cells per row as the width allows
    // and wraps.
    const int labelH = s(kLabelH);
    // The height the controls need at one knob size: the cells flow left to right, wrapping at the width or at a row break.
    auto heightAt = [&](int knobCellW, int knobCellH)
    {
        const int cellW = knobCellW + s(6), cellH = labelH + knobCellH + s(2);
        int x = 0, rows = 1;
        for (const auto& c : controls)
        {
            const int cw = c->combo ? 2 * cellW : cellW;
            if (x > 0 && (c->newRow || x + cw > area.getWidth())) { x = 0; ++rows; }
            x += cw;
        }
        return rows * cellH;
    };
    // Size 2 unless the controls do not fit the area there; then the first smaller standard size (3, then 4) that does.
    int knobW = s(MuLookAndFeel::kKnobSize2W), knobH = s(MuLookAndFeel::kKnobSize2H);
    if (heightAt(knobW, knobH) > area.getHeight())
    {
        const int sizesW[] = { s(MuLookAndFeel::kKnobSize3W), s(MuLookAndFeel::kKnobSize4W) };
        const int sizesH[] = { s(MuLookAndFeel::kKnobSize3H), s(MuLookAndFeel::kKnobSize4H) };
        for (int k = 0; k < 2; ++k)
            if (heightAt(sizesW[k], sizesH[k]) <= area.getHeight()) { knobW = sizesW[k]; knobH = sizesH[k]; break; }
    }
    const int comboH = s(24);
    const int cellW  = knobW + s(6);
    const int cellH  = labelH + knobH + s(2);

    // Flow the cells left to right, wrapping at the width. A selector takes two cells so its
    // text fits (the metal style's LCD lettering is wider than one knob).
    int y = area.getY();
    const int total = (int) controls.size();
    for (int first = 0; first < total;)
    {
        // Phase 1: the run of cells that makes this row, its width and its number of groups.
        auto cellWidth = [&](int k) { return controls[(size_t) k]->combo ? 2 * cellW : cellW; };
        int last = first, rowW = 0, groups = 0;
        while (last < total)
        {
            const int cw = cellWidth(last);
            if (last > first && (controls[(size_t) last]->newRow || rowW + cw > area.getWidth())) break;
            rowW += cw;
            if (controls[(size_t) last]->groupStart) ++groups;
            ++last;
        }

        // Phase 2: a row with groups spreads its spare width as equal gaps before, between and after them.
        const int gap = groups > 0 ? juce::jmax(0, (area.getWidth() - rowW) / (groups + 1)) : 0;
        int x = area.getX() + (groups > 0 ? gap : 0);
        for (int k = first; k < last; ++k)
        {
            auto& ctl = controls[(size_t) k];
            if (ctl->groupStart && k > first) x += gap;
            const int cw = cellWidth(k);
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
        y += cellH;
        first = last;
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
