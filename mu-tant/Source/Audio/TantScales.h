#pragma once

#include "Audio/MusicalScales.h"   // mu-core: the family scale table

// mu-Tant's scale-quantised pitch (design-voice.md "Pitch — scale-quantised") uses the family
// scale table; only the base octave below is mu-Tant's own.
namespace mu_tant
{
using mu_audio::Scale;
using mu_audio::kScales;
using mu_audio::kNumScales;
using mu_audio::clampScaleIndex;
using mu_audio::scaleSemitone;
using mu_audio::toneToMidi;
using mu_audio::midiToFreq;

// Base octave the per-osc octave offset (-3..+3) sits on, so the playable range lands in an
// audible drone register (octave 0, root C → MIDI 48 = C3). Shared by the voice engine
// (free-mode pitch) and the processor (Note-mode pitch-tracking).
inline constexpr int kBaseOctave = 4;
} // namespace mu_tant
