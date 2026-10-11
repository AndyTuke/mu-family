#pragma once

namespace mu_clid_midi {

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

} // namespace mu_clid_midi
