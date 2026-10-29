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
    currentPage = juce::jlimit(0, pages - 1, currentPage);   // a row that got shorter may have taken its page away
    pageLabel.setText("Page " + juce::String(currentPage + 1) + " / " + juce::String(pages), juce::dontSendNotification);
    prevButton.setEnabled(currentPage > 0);
    nextButton.setEnabled(currentPage < pages - 1);
    if (auto* row = lengthRow ? lengthRow() : nullptr) lengthInput.setValue(row->numSteps(), false);
}

// Prev, the page, Next, and the Length at the right.
void StepPager::resized()
{
    auto r = getLocalBounds().reduced(0, mu_ui::s(2));
    prevButton.setBounds(r.removeFromLeft(mu_ui::s(64)));
    r.removeFromLeft(mu_ui::s(6));
    pageLabel.setBounds(r.removeFromLeft(mu_ui::s(90)));
    r.removeFromLeft(mu_ui::s(6));
    nextButton.setBounds(r.removeFromLeft(mu_ui::s(64)));
    lengthInput.setBounds(r.removeFromRight(mu_ui::s(150)));
}

} // namespace mu_on
