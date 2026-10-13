#pragma once
#include <juce_graphics/juce_graphics.h>
#include <algorithm>
#include <utility>

// Pure geometry for the linear-slider fader cap and groove. No LookAndFeel, no drawing, no
// theme: MuLookAndFeel::drawLinearSlider feeds it the (already scaled) constants and paints
// what it returns, so the layout can be unit-tested without a Graphics context.
namespace mu_ui::fader
{
    // The cap's two extents: `thickness` across the travel, `length` along it.
    struct CapSize
    {
        float thickness = 0.0f;
        float length    = 0.0f;
    };

    // Nominal size when the slider is wide enough; otherwise the thickness clamps to the
    // cross axis minus `margin` and the length follows at `aspect` x thickness.
    inline CapSize capSize(float nominalThickness, float nominalLength, float crossAxis,
                           float margin, float aspect) noexcept
    {
        const float room = std::max(0.0f, crossAxis - margin);
        if (nominalThickness <= room)
            return { nominalThickness, nominalLength };
        return { room, room * aspect };
    }

    // Half the cap length: the slider's thumb radius, so the cap never leaves its bounds.
    inline float thumbRadius(const CapSize& cap) noexcept { return cap.length * 0.5f; }

    // Pixel position of the cap centre at which the value is at the minimum / maximum end.
    // Vertical sliders run bottom (min) to top (max); horizontal run left to right.
    inline std::pair<float, float> travelEnds(juce::Rectangle<float> area, bool horizontal,
                                              float thumbR) noexcept
    {
        if (horizontal)
            return { area.getX() + thumbR, area.getRight() - thumbR };
        return { area.getBottom() - thumbR, area.getY() + thumbR };
    }

    // Pixel position along the travel for a 0..1 proportion of the range.
    inline float positionForProportion(std::pair<float, float> ends, float proportion) noexcept
    {
        return ends.first + proportion * (ends.second - ends.first);
    }

    // The cap rectangle: centred across the area, centred on `sliderPos` along the travel.
    inline juce::Rectangle<float> capRect(juce::Rectangle<float> area, bool horizontal,
                                          float sliderPos, const CapSize& cap) noexcept
    {
        if (horizontal)
            return { sliderPos - cap.length * 0.5f, area.getCentreY() - cap.thickness * 0.5f,
                     cap.length, cap.thickness };
        return { area.getCentreX() - cap.thickness * 0.5f, sliderPos - cap.length * 0.5f,
                 cap.thickness, cap.length };
    }

    // The recessed groove: `trackW` across, centred in the area, spanning the cap-centre travel.
    inline juce::Rectangle<float> grooveRect(juce::Rectangle<float> area, bool horizontal,
                                             float trackW, std::pair<float, float> ends) noexcept
    {
        const float lo = std::min(ends.first, ends.second);
        const float hi = std::max(ends.first, ends.second);
        if (horizontal)
            return { lo, area.getCentreY() - trackW * 0.5f, hi - lo, trackW };
        return { area.getCentreX() - trackW * 0.5f, lo, trackW, hi - lo };
    }

    // The accent fill along the travel as a sorted {low, high} pixel span: from the minimum end
    // up to the cap, or from the zero position to the cap on a bipolar slider.
    inline std::pair<float, float> fillSpan(float minEndPos, float zeroPos, float sliderPos,
                                            bool bipolar) noexcept
    {
        const float from = bipolar ? zeroPos : minEndPos;
        return { std::min(from, sliderPos), std::max(from, sliderPos) };
    }

    // A slider is bipolar when its range straddles zero (the depth slider, -100..+100).
    inline bool isBipolar(double minimum, double maximum) noexcept
    {
        return minimum < 0.0 && maximum > 0.0;
    }
}
