#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ValueFormat.h"   // mu-core root: displayMagnitude
#include <functional>

// Float-parameter text attributes with BOTH directions, so a host that sends text back (a typed
// value in an automation lane, CLAP / VST3 text round-trip checks) gets the value the displayed text
// stands for. The product supplies only the value-to-text format; the inverse is derived from it.
namespace mu_fmt
{

// The value in `range` whose displayed text is `text`. First it looks for an exact match by scanning
// the range's normalised position (so a skewed range works) and answers the middle of the matching
// run: an edge value can round the other way after the host converts it to 0..1 and back. A text no
// value shows exactly falls back to the closest displayed size (displayMagnitude), by bisection, which
// needs that size to rise or fall steadily with the value. Runs on text entry only, never on the audio thread.
inline float valueFromText(const juce::String& text, const juce::NormalisableRange<float>& range,
                           const std::function<juce::String(float, int)>& toText)
{
    auto at = [&](float pos01) { return range.snapToLegalValue(range.convertFrom0to1(pos01)); };
    const auto text0 = text.trim();

    // Phase 1: exact text match; keep the first contiguous run of matching positions.
    constexpr int kScan = 8192;
    int runStart = -1, runEnd = -1;
    for (int i = 0; i <= kScan; ++i)
    {
        const bool hit = toText(at((float) i / (float) kScan), 0).trim() == text0;
        if (hit && runStart < 0) runStart = i;
        if (hit) runEnd = i;
        if (! hit && runStart >= 0) break;
    }
    if (runStart >= 0) return at((float) ((runStart + runEnd) / 2) / (float) kScan);

    // Phase 2: no value shows this text; bisect on the displayed size.
    auto mag = [&](float pos01) { return displayMagnitude(toText(at(pos01), 0)); };
    const double target = displayMagnitude(text);
    const bool   rising = mag(1.0f) >= mag(0.0f);
    float lo = 0.0f, hi = 1.0f;
    for (int i = 0; i < 40; ++i)   // 2^-40 of the range: finer than any displayed step
    {
        const float mid = 0.5f * (lo + hi);
        const bool below = rising ? mag(mid) < target : mag(mid) > target;
        (below ? lo : hi) = mid;
    }
    return std::abs(mag(hi) - target) <= std::abs(mag(lo) - target) ? at(hi) : at(lo);
}

inline juce::AudioParameterFloatAttributes floatText(const juce::NormalisableRange<float>& range,
                                                     std::function<juce::String(float, int)> toText)
{
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction(toText)
        .withValueFromStringFunction([range, toText](const juce::String& text) { return valueFromText(text, range, toText); });
}

} // namespace mu_fmt
