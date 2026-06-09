#pragma once

// The fixed mu-On instrument lanes — shared by the processor, the engines, and the UI.
// Kick/Bass/Hat/Snare are step-sequenced instruments (kNumTracks in the StepPattern);
// Rumble is a processor lane (no step row) — it takes the Kick's audio as a feed and runs
// it through drive/delays/reverb/envelope/filter.
namespace mu_on
{
enum Channel { Kick = 0, Bass = 1, Hat = 2, Snare = 3, Rumble = 4, kNumChannels = 5 };

// Number of step-sequenced lanes (Kick..Snare). Rumble has no step row.
inline constexpr int kNumStepLanes = Rumble;   // = 4
}
