#pragma once

#include <juce_core/juce_core.h>

// The two clamps the paged Steps grid applies when a row gets shorter (a Length edit or a preset swap). Pure
// functions so they are unit-tested without the GUI: StepPager and TrackContent call them, and so does the
// mu-Tronic Steps box through the same classes.
namespace mu_on
{

// The page a row of `pages` pages can still show: a row that got shorter may have taken its page away.
inline int clampPage(int page, int pages) noexcept
{
    return juce::jlimit(0, juce::jmax(1, pages) - 1, page);
}

// The step the Step panel may still edit in a row of `numSteps` steps: never one past the Length.
inline int clampStep(int step, int numSteps) noexcept
{
    return juce::jlimit(0, juce::jmax(1, numSteps) - 1, step);
}

} // namespace mu_on
