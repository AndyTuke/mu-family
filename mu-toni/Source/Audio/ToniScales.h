#pragma once

#include "Audio/MusicalScales.h"   // mu-core: the family scale table

// mu-Toni uses the family scale table for its arp root palette and the optional diatonic
// snap of chord tones (the arp uses absolute chords, so the scale doesn't set chord quality).
namespace mu_toni
{
using mu_audio::Scale;
using mu_audio::kScales;
using mu_audio::kNumScales;
using mu_audio::clampScaleIndex;
using mu_audio::snapToScale;
using mu_audio::midiToFreq;
} // namespace mu_toni
