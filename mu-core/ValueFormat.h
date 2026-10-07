#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>

// The family's value-to-text formats, shared by parameter text (host automation lanes) and
// knob / status-bar display so the same value always reads the same. `unit` = false gives
// the bare number for a knob whose label already carries the unit.
namespace mu_fmt
{

// A time in seconds: "N ms" below one second, "N.NN s" from there (1 ms floor) — ADSR times.
inline juce::String time(double sec, bool unit = true)
{
    const double ms = std::max(1.0, sec * 1000.0);
    if (ms < 1000.0) return juce::String((int) std::round(ms)) + (unit ? " ms" : "");
    return juce::String(ms / 1000.0, 2) + (unit ? " s" : "");
}

// The inverse of time(): "120 ms", "1.2 s" or a bare number (taken as ms) → seconds.
inline double parseTime(const juce::String& s)
{
    const auto t = s.trim().toLowerCase();
    if (t.endsWith("ms")) return t.dropLastCharacters(2).trim().getDoubleValue() / 1000.0;
    if (t.endsWith("s"))  return t.dropLastCharacters(1).trim().getDoubleValue();
    return t.getDoubleValue() / 1000.0;
}

// A frequency: whole Hz below 1 kHz, then kHz to `kHzDecimals` places.
inline juce::String freq(double hz, bool unit = true, int kHzDecimals = 2)
{
    if (hz < 1000.0) return juce::String((int) std::round(hz)) + (unit ? " Hz" : "");
    return juce::String(hz / 1000.0, kHzDecimals) + (unit ? " kHz" : "");
}

// A low-cut filter frequency: "Off" at 0, else freq().
inline juce::String lowCut(double hz, bool unit = true)
{
    return hz <= 0.0 ? juce::String("Off") : freq(hz, unit, 2);
}

// A 0..1 amount as a whole percentage number (no sign).
inline juce::String percent(double v01)
{
    return juce::String((int) std::round(v01 * 100.0));
}

} // namespace mu_fmt
