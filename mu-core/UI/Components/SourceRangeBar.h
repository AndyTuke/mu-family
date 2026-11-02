#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include <functional>
#include <memory>
#include <vector>

// An overview of a whole source sample with a draggable range: the part inside the range is the section a layer uses, the rest is
// dimmed and ignored. Drag a handle to move that end, drag inside the range to slide it. The component edits nothing itself: it
// reports the range on release and shows what it is given. The audio is summarised once into peak columns, so a repaint costs one
// column of work per pixel whatever the sample's length.
class SourceRangeBar : public juce::Component, public juce::SettableTooltipClient
{
public:
    SourceRangeBar() = default;

    // The whole source to show (mono mix for display); nullptr clears it. Resets the range to all of it.
    void setAudio(std::shared_ptr<const juce::AudioBuffer<float>> audio);
    // The used range in samples of the source; an end of 0 or less means "to the end".
    void setRange(int start, int end);
    int  getStart() const noexcept { return rangeStart; }
    int  getEnd() const noexcept   { return rangeEnd; }
    int  getLength() const noexcept { return length; }

    // The smallest range a drag can leave, in samples.
    void setMinRange(int samples) noexcept { minRange = juce::jmax(1, samples); }

    // The range was changed by a drag and released.
    std::function<void(int start, int end)> onRangeChanged;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;

private:
    enum class Grab { None, Start, End, Slide };
    int  xOfSample(int sample) const noexcept;
    int  sampleAtX(int x) const noexcept;
    Grab grabAt(int x) const noexcept;

    std::vector<float> peaks;       // 0..1, one per column of the whole source
    int length = 0;                 // samples in the source
    int rangeStart = 0, rangeEnd = 0;
    int minRange = 2000;
    Grab grab = Grab::None;
    int  grabOffset = 0;            // samples between the pointer and the grabbed edge (or the range start, for a slide)
    bool dragged = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SourceRangeBar)
};
