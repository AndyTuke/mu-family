#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>

namespace mu_core {

// The two MIDI velocities (0..1) an Accent setting (0..100) gives: one for accented hits, one for the rest.
struct AccentVelocities { float accented; float normal; };

// Bottom half (0-50): accented ramps 100 -> 127, plain hits stay at 100.
// Top half (50-100): accented stays at 127, plain hits ramp 100 -> 75.
inline AccentVelocities accentVelocities(float accentAmt) noexcept
{
    if (accentAmt <= 50.0f)
    {
        const float t = accentAmt / 50.0f;
        return { (100.0f + t * 27.0f) / 127.0f, 100.0f / 127.0f };
    }
    const float t = (accentAmt - 50.0f) / 50.0f;
    return { 1.0f, (100.0f - t * 25.0f) / 127.0f };
}

// The velocity (0..1) of one MIDI-output hit: the Accent shape above, scaled by the Velocity setting
// (1..100 %). Never below 1.5/127 so the engine's truncation to 0..127 cannot reach 0 (a note-on at
// velocity 0 is a note-off).
inline float midiOutVelocity(int velocityPct, float accentAmt, bool accented) noexcept
{
    const auto v    = accentVelocities(accentAmt);
    const float raw = (accented ? v.accented : v.normal) * (float) std::clamp(velocityPct, 1, 100) * 0.01f;
    return std::clamp(raw, 1.5f / 127.0f, 1.0f);
}

// "C-1" .. "G9" style name of a MIDI note (60 = C4), for the note selectors.
inline juce::String midiNoteName(int note)
{
    static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String(names[((note % 12) + 12) % 12]) + juce::String(note / 12 - 1);
}

} // namespace mu_core
