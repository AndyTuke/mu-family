#pragma once

#include <juce_core/juce_core.h>

#include <cmath>
#include <optional>

// The timeline length as text: what the length box shows and what it accepts. A length is typed in bars, beats or as a note
// value ("2 bars", "6 beats", "6", "3/4", "1/16"): a fraction is a share of a whole note (a whole note is four beats), so "3/4"
// is three beats and "6/8" is three beats too. A bare number is in the unit the box was showing.
namespace mu_tilate
{

// The length in beats from the text, or nothing when it is not understood or not above zero.
inline std::optional<double> parseLengthBeats(const juce::String& text, double bareNumberBeats = 1.0)
{
    auto t = text.trim().toLowerCase();
    if (t.isEmpty()) return std::nullopt;

    // A fraction: N/D (optionally followed by "note" or "notes").
    const int slash = t.indexOfChar('/');
    if (slash >= 0)
    {
        const double n = t.substring(0, slash).trim().getDoubleValue();
        const double d = t.substring(slash + 1).retainCharacters("0123456789.").getDoubleValue();
        if (n > 0.0 && d > 0.0 && t.substring(0, slash).trim().containsOnly("0123456789.")) return n / d * 4.0;
        return std::nullopt;
    }

    if (! juce::CharacterFunctions::isDigit(t[0]) && t[0] != '.') return std::nullopt;   // no sign, no stray text before the number
    const auto number = t.retainCharacters("0123456789.");
    if (number.isEmpty() || number.indexOfChar('.') != number.lastIndexOfChar('.')) return std::nullopt;
    const double v = number.getDoubleValue();
    if (! (v > 0.0)) return std::nullopt;

    // Everything after the number is the unit; anything but bars or beats is rejected.
    const auto unit = t.fromFirstOccurrenceOf(number, false, false).trim();
    if (unit.isEmpty()) return v * bareNumberBeats;
    if (unit.startsWith("bar")) return v * 4.0;
    if (unit == "b" || unit.startsWith("beat")) return v;
    return std::nullopt;
}

// The text for a length: whole bars as "N bars", anything else in beats.
inline juce::String formatLengthBeats(double beats)
{
    const double bars = beats / 4.0;
    if (std::abs(bars - std::round(bars)) < 1.0e-6)
    {
        const int n = (int) std::lround(bars);
        return juce::String(n) + (n == 1 ? " bar" : " bars");
    }
    auto s = juce::String(beats, 3);
    while (s.endsWithChar('0')) s = s.dropLastCharacters(1);
    if (s.endsWithChar('.')) s = s.dropLastCharacters(1);
    return s + (s == "1" ? " beat" : " beats");
}

} // namespace mu_tilate
