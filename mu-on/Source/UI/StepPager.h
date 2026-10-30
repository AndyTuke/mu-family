#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Sequencer/StepRow.h"
#include "UI/Components/NudgeInput.h"
#include "UI/StepPaging.h"

#include <functional>

// StepPager — the strip under a step grid: Prev / Next through the 16-step pages, the page, and the Length of the row it
// edits (1 to 64 steps). The one implementation for mu-On's StepRows and for the single row of a mu-Tronic Track layer, so
// the two products page and set the length the same way. The owner says how many pages there are and which row the Length
// edits, calls refresh() when either may have changed, and shows page() through its callback. Metal style only.
namespace mu_on
{

class StepPager : public juce::Component
{
public:
    StepPager();

    // Unscaled height of the strip.
    static constexpr int kHeight = 28;

    std::function<int()>      pageCount;    // pages the shown rows need (at least 1)
    std::function<StepRow*()> lengthRow;    // the row the Length edits (null: none)
    std::function<void()>     onChanged;    // the page or a length changed: redraw the grid

    int  page() const noexcept { return currentPage; }
    void setPage(int p);
    void refresh();                         // clamp the page, update the text, the buttons and the Length shown

    void resized() override;

private:
    int currentPage = 0;
    juce::TextButton prevButton { "Prev" }, nextButton { "Next" };
    juce::Label      pageLabel;
    NudgeInput       lengthInput { "Length", 1, StepRow::kMaxSteps, StepRow::kNumSteps };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepPager)
};

} // namespace mu_on
