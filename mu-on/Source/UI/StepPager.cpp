#include "StepPager.h"
#include "UI/Components/MuLookAndFeel.h"

namespace mu_on
{

StepPager::StepPager()
{
    prevButton.onClick = [this] { setPage(currentPage - 1); };
    nextButton.onClick = [this] { setPage(currentPage + 1); };
    pageLabel.setJustificationType(juce::Justification::centred);
    lengthInput.setShowStepButtons(false);   // the arrows step by one; a double-click types a length
    lengthInput.setLabelInline(true);
    resetInput.setShowStepButtons(false);
    resetInput.setLabelInline(true);
    resetInput.setZeroText("Off");
    resetInput.onChange = [this](int n)
    {
        setSequenceParam(kSeqResetParamId, n);
        if (onStatusUpdate) onStatusUpdate("Sequence Reset", n == 0 ? juce::String("Off") : juce::String(n) + (n == 1 ? " bar" : " bars"));
    };
    lengthInput.onChange = [this](int n)
    {
        if (sequence != nullptr)
        {
            setSequenceParam(kSeqBarsParamId, n);
            refresh();
            if (onChanged) onChanged();
            if (onStatusUpdate) onStatusUpdate("Sequence Length", juce::String(n) + (n == 1 ? " bar" : " bars"));
            return;
        }
        if (auto* row = lengthRow ? lengthRow() : nullptr) row->setNumSteps(n);
        refresh();
        if (onChanged) onChanged();
        if (onStatusUpdate) onStatusUpdate("Step Length", juce::String(n) + (n == 1 ? " step" : " steps"));
    };
    addAndMakeVisible(prevButton);
    addAndMakeVisible(nextButton);
    addAndMakeVisible(pageLabel);
    addAndMakeVisible(lengthInput);
    addChildComponent(resetInput);
    refresh();
}

void StepPager::useSequenceParams(juce::AudioProcessorValueTreeState& apvts)
{
    sequence = apvts.getParameter(kSeqBarsParamId) != nullptr ? &apvts : nullptr;
    lengthInput.setRange(1, 8);
    lengthInput.setLabel("Length (bars)");
    resetInput.setVisible(sequence != nullptr);
    resized();
    refresh();
}

void StepPager::setSequenceParam(const char* id, int value)
{
    if (sequence == nullptr) return;
    if (auto* p = sequence->getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1((float) value));
}

int StepPager::sequenceParam(const char* id, int fallback) const
{
    if (sequence == nullptr) return fallback;
    if (auto* v = sequence->getRawParameterValue(id)) return juce::roundToInt(v->load());
    return fallback;
}

void StepPager::setPage(int p)
{
    currentPage = p;
    refresh();
    if (onChanged) onChanged();
}

void StepPager::refresh()
{
    const int pages = pageCount ? juce::jmax(1, pageCount()) : 1;
    currentPage = clampPage(currentPage, pages);   // a row that got shorter may have taken its page away
    pageLabel.setText("Page " + juce::String(currentPage + 1) + " / " + juce::String(pages), juce::dontSendNotification);
    prevButton.setEnabled(currentPage > 0);
    nextButton.setEnabled(currentPage < pages - 1);
    if (sequence != nullptr)
    {
        if (lengthInput.getValue() != sequenceParam(kSeqBarsParamId, 1)) lengthInput.setValue(sequenceParam(kSeqBarsParamId, 1), false);
        if (resetInput.getValue()  != sequenceParam(kSeqResetParamId, 0)) resetInput.setValue(sequenceParam(kSeqResetParamId, 0), false);
        return;
    }
    if (auto* row = lengthRow ? lengthRow() : nullptr) lengthInput.setValue(row->numSteps(), false);
}

// Prev, the page, Next, and the Length at the right.
void StepPager::resized()
{
    using LF = MuLookAndFeel;
    auto r = getLocalBounds().reduced(0, mu_ui::s(LF::kPagerPad));
    prevButton.setBounds(r.removeFromLeft(mu_ui::s(LF::kPagerButtonW)));
    r.removeFromLeft(mu_ui::s(LF::kPagerGap));
    pageLabel.setBounds(r.removeFromLeft(mu_ui::s(LF::kPagerLabelW)));
    r.removeFromLeft(mu_ui::s(LF::kPagerGap));
    nextButton.setBounds(r.removeFromLeft(mu_ui::s(LF::kPagerButtonW)));
    lengthInput.setBounds(r.removeFromRight(mu_ui::s(LF::kPagerLengthW)));
    if (sequence != nullptr) { r.removeFromRight(mu_ui::s(LF::kPagerGap)); resetInput.setBounds(r.removeFromRight(mu_ui::s(LF::kPagerLengthW))); }
}

} // namespace mu_on
