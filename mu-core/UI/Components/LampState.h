#pragma once
#include "MuLookAndFeel.h"
#include <cmath>

// The family's indicator-lamp states and how brightly each one is lit: shared by StepIndicator (the
// Performance mode preset lamps) and ClipStrip (the layer's eight clip pads), so a lamp means the same
// thing wherever it appears (docs/design-ui-family.md 12.2 and 13.2). Pure functions, no display needed.
namespace mu_ui
{

// Empty = no item there, Available = playable / stored, Current = playing, Queued = waiting for its boundary
// (pulses), Fault = missing file or another product's item (steady), MutedCurrent = playing but its layer is
// muted (a hard square flash, unlike the smooth queued pulse).
enum class LampState { Empty, Available, Current, Queued, Fault, MutedCurrent };

// A smooth 0..1 pulse at `hz` for a time in milliseconds (0 at t = 0).
inline float lampPulse(double timeMs, float hz) noexcept
{
    return 0.5f - 0.5f * (float) std::cos(juce::MathConstants<double>::twoPi * (double) hz * timeMs / 1000.0);
}

// The lit amount (Lighting lampOff .. lampOn + lampPlayhead) a lamp shows now; `flash` adds the
// playhead brightness for a lamp that has just become current.
inline float lampLit(LampState state, double timeMs, bool flash) noexcept
{
    const auto& L = MuLookAndFeel::lighting();
    const float bright = juce::jmin(1.0f, L.lampOn + L.lampPlayhead);
    switch (state)
    {
        case LampState::Empty:        return L.lampOff;
        case LampState::Available:    return L.lampDim;
        case LampState::Current:      return flash ? bright : L.lampOn;
        case LampState::Queued:       return L.lampDim + (bright - L.lampDim) * lampPulse(timeMs, L.queuedPulseHz);
        case LampState::Fault:        return L.lampDim;
        case LampState::MutedCurrent:
        {
            // 50 % duty square wave between lit and dim; the dim half is never lampOff, so it never reads as empty.
            const double phase = std::fmod(timeMs * (double) L.clipMutedFlashHz / 1000.0, 1.0);
            return phase < 0.5 ? L.lampOn : L.lampDim;
        }
    }
    return L.lampOff;
}

} // namespace mu_ui
