#pragma once

// ClockLock — when the external-clock phase servo counts as locked or drifting.
//
// The servo slews the master's beat toward the incoming MIDI clock's grid. The lamp in the transport
// bar shows Locked while the remaining phase error is small and Drifting while the servo is still
// pulling the grid back (after a tempo change or a Start). The thresholds are here, in the clock
// code, so the drawing code holds no numbers (docs/mu-link/design-ui.md section 4).
namespace mu_link
{

// Above this phase error (half a clock pulse: 1 / 48 beat) the grid counts as drifting.
inline constexpr double kDriftThresholdBeats = 1.0 / 48.0;

// A drifting grid counts as locked again only below this error (a quarter pulse: 1 / 96 beat).
// The gap to kDriftThresholdBeats is the hysteresis that keeps the lamp from flickering.
inline constexpr double kRelockThresholdBeats = 1.0 / 96.0;

// The next lamp state for the current one and the absolute phase error, in beats.
inline constexpr bool nextDriftingState(bool drifting, double absErrorBeats) noexcept
{
    return drifting ? ! (absErrorBeats < kRelockThresholdBeats)
                    : absErrorBeats > kDriftThresholdBeats;
}

} // namespace mu_link
