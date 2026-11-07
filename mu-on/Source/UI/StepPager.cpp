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
    lengthInput.onChange = [this](int n)
    {
        if (auto* row = lengthRow ? lengthRow() : nullptr) row->setNumSteps(n);
        refresh();
        if (onChanged) onChanged();
        if (onStatusUpdate) onStatusUpdate("Step Length", juce::String(n) + (n == 1 ? " step" : " steps"));
    };
    addAndMakeVisible(prevButton);
    addAndMakeVisible(nextButton);
    addAndMakeVisible(pageLabel);
    addAndMakeVisible(lengthInput);
    refresh();
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
}

} // namespace mu_on
