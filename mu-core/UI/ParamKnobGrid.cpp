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
        ctl->small = spec.small;
        ctl->statusName = spec.statusName.isNotEmpty() ? spec.statusName : spec.label;

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

namespace
{
    // The scaled knob cell of a standard size index: 0 = Size 2, 1 = Size 3, 2 = Size 4.
    struct KnobDims { int w, h; };
    KnobDims knobDims(int index)
    {
        using mu_ui::s;
        switch (juce::jlimit(0, 2, index))
        {
            case 0:  return { s(MuLookAndFeel::kKnobSize2W), s(MuLookAndFeel::kKnobSize2H) };
            case 1:  return { s(MuLookAndFeel::kKnobSize3W), s(MuLookAndFeel::kKnobSize3H) };
            default: return { s(MuLookAndFeel::kKnobSize4W), s(MuLookAndFeel::kKnobSize4H) };
        }
    }
}

// The height the controls need at one knob size: the cells flow left to right, wrapping at the width or at a row break; a row is
// as tall as its tallest control (a small knob is always Size 4).
int ParamKnobGrid::heightAt(int sizeIndex, int areaWidth) const
{
    using mu_ui::s;
    const auto base = knobDims(sizeIndex), small = knobDims(2);
    const int labelH = s(kLabelH), cellW = base.w + s(6);
    int x = 0, rowH = 0, total = 0;
    for (const auto& c : controls)
    {
        const int cw = c->combo ? 2 * cellW : cellW;
        if (x > 0 && (c->newRow || x + cw > areaWidth)) { total += rowH; rowH = 0; x = 0; }
        x += cw;
        rowH = juce::jmax(rowH, labelH + (c->small ? small.h : base.h) + s(2));
    }
    return total + rowH;
}

int ParamKnobGrid::fitIndex() const
{
    if (controls.empty()) return 0;
    const auto area = getLocalBounds();
    // Size 2 (the per-channel default used across mu-clid/mu-tant) unless the controls do not fit the area there; then the
    // first smaller standard size (3, then 4) that does.
    for (int index = 0; index < 2; ++index)
        if (heightAt(index, area.getWidth()) <= area.getHeight()) return index;
    return 2;
}

void ParamKnobGrid::resized()
{
    using mu_ui::s;
    using mu_ui::sf;
    auto area = getLocalBounds();
    if (controls.empty()) return;

    // Knobs are drawn at one of the family's standard sizes, not stretched to the cell: the largest that fits the area, and
    // never bigger than the owner's floor (so two grids side by side match). The grid then fits as many standard cells per row
    // as the width allows and wraps.
    const int labelH = s(kLabelH);
    const int sizeIndex = juce::jlimit(0, 2, juce::jmax(fitIndex(), minSizeIndex));
    const auto base = knobDims(sizeIndex), small = knobDims(2);
    const int comboH = s(24);
    const int cellW  = base.w + s(6);

    // Flow the cells left to right, wrapping at the width. A selector takes two cells so its
    // text fits (the metal style's LCD lettering is wider than one knob).
    int y = area.getY();
    const int total = (int) controls.size();
    for (int first = 0; first < total;)
    {
        // Phase 1: the run of cells that makes this row, its width, its number of groups and its height.
        auto cellWidth = [&](int k) { return controls[(size_t) k]->combo ? 2 * cellW : cellW; };
        int last = first, rowW = 0, groups = 0, rowH = 0;
        while (last < total)
        {
            const int cw = cellWidth(last);
            if (last > first && (controls[(size_t) last]->newRow || rowW + cw > area.getWidth())) break;
            rowW += cw;
            if (controls[(size_t) last]->groupStart) ++groups;
            rowH = juce::jmax(rowH, labelH + (controls[(size_t) last]->small ? small.h : base.h) + s(2));
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
            juce::Rectangle<int> cell(x, y, cw, rowH);
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
                const auto dims = ctl->small ? small : base;
                const auto dial = cell.withSizeKeepingCentre(dims.w, dims.h);
                ctl->knob->setBounds(dial.withTrimmedTop(-s(MuLookAndFeel::kKnobTopPad))
                                         .withTrimmedBottom(-s(MuLookAndFeel::kKnobLabelH)));
            }
        }
        y += rowH;
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
    if (ctl.knob)       onStatusUpdate(ctl.statusName, ctl.knob->getSlider().getTextFromValue(ctl.knob->getValue()));
    else if (ctl.combo) onStatusUpdate(ctl.statusName, ctl.combo->getText());
}

} // namespace mu_ui
