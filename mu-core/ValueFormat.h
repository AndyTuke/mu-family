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

// The size of a displayed value, for ordering: "1.00 kHz" -> 1000, "47 ms" -> 0.047, "10.00 s" -> 10,
// "Off" -> 0; any other text is its leading number ("-12 dB", "5 %", "0.3 st").
inline double displayMagnitude(const juce::String& text)
{
    const auto t = text.trim().toLowerCase();
    if (t == "off") return 0.0;
    const double n = t.getDoubleValue();
    if (t.endsWith("khz")) return n * 1000.0;
    if (t.endsWith("ms"))  return n / 1000.0;
    return n;
}

// ── Units ────────────────────────────────────────────────────────────────────
// What a knob's value means, so the status bar and the host's parameter text always read "number + unit"
// (the knob's label never carries the unit). Times are held in the unit the parameter stores: Ms in
// milliseconds, Seconds in seconds; Hz in hertz; Percent01 a 0..1 amount, Percent100 a 0..100 amount.
enum class Unit { Plain, Ms, Seconds, Hz, HzNote, Percent01, Percent100, Db, Semitones, Cents, Octaves, Ratio, Note };

// The note name of a MIDI note number, with its octave (middle C = C4).
inline juce::String noteWithOctave(int midi)
{
    static const char* const kNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int n = ((midi % 12) + 12) % 12;
    return juce::String(kNames[n]) + juce::String((int) std::floor(midi / 12.0) - 1);
}

// The pitch class of 0..11 on its own ("C", "C#" ...), for a root-note knob.
inline juce::String pitchClass(int pc)
{
    static const char* const kNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return kNames[((pc % 12) + 12) % 12];
}

// A signed number: whole when it is whole, else one decimal ("+3", "-2.5", "0").
inline juce::String signedNumber(double v)
{
    const bool whole = std::abs(v - std::round(v)) < 0.05;
    const juce::String n = whole ? juce::String((int) std::round(v)) : juce::String(v, 1);
    return (v > 0.0 && n.isNotEmpty() && n[0] != '-' ? "+" : "") + n;
}

// A time in milliseconds: "0.5 ms" under 10, whole ms to a second, then "N.NN s".
inline juce::String milliseconds(double ms)
{
    if (ms < 0.05)    return "0 ms";
    if (ms < 10.0)    return juce::String(ms, 1) + " ms";
    if (ms < 1000.0)  return juce::String((int) std::round(ms)) + " ms";
    return juce::String(ms / 1000.0, 2) + " s";
}

// A frequency with one decimal under 100 Hz (a bass tuning needs it), whole Hz to a kilohertz, then kHz.
inline juce::String hertz(double hz)
{
    if (hz < 100.0) return juce::String(hz, 1) + " Hz";
    return freq(hz, true, 2);
}

// The value `v` in `u`, number and unit together.
inline juce::String unitText(Unit u, double v)
{
    switch (u)
    {
        case Unit::Ms:         return milliseconds(v);
        case Unit::Seconds:    return milliseconds(v * 1000.0);
        case Unit::Hz:         return hertz(v);
        case Unit::HzNote:
        {
            const int midi = (int) std::lround(69.0 + 12.0 * std::log2(std::max(1.0e-3, v) / 440.0));
            return hertz(v) + " (" + noteWithOctave(midi) + ")";
        }
        case Unit::Percent01:  return juce::String((int) std::round(v * 100.0)) + " %";
        case Unit::Percent100: return juce::String((int) std::round(v)) + " %";
        case Unit::Db:         return v <= -59.95 ? juce::String("-inf dB") : juce::String(v, 1) + " dB";
        case Unit::Semitones:  return signedNumber(v) + " st";
        case Unit::Cents:      return signedNumber(v) + " ct";
        case Unit::Octaves:    return signedNumber(v) + " oct";
        case Unit::Ratio:      return juce::String(v, 2) + " x";
        case Unit::Note:       return pitchClass((int) std::lround(v));
        case Unit::Plain:      break;
    }
    return juce::String(v, 2);
}

} // namespace mu_fmt
