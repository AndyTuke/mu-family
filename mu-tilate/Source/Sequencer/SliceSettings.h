#pragma once

// What one slice plays like (docs/mu-tilate/design-ui.md, "Slice player"): the settings the slice player panel edits for the
// selected slice, kept with the slice's marker so they follow it when markers are added, moved or removed. Plain values, saved
// with the session. The defaults are neutral: a slice with default settings plays as the source.
namespace mu_tilate
{

struct SliceSettings
{
    // Slice
    bool  reverse   = false;    // plays backwards; a timeline block that is reversed too cancels it out
    float gain      = 1.0f;     // linear, 0..2
    float pan       = 0.0f;     // -1 (left) .. 1 (right)
    float fadeInMs  = 0.0f;     // added to the layer's fade in
    float fadeOutMs = 0.0f;     // added to the layer's fade out

    // Pitch
    float pitchSemi = 0.0f;     // -24..24 semitones
    float fineCents = 0.0f;     // -100..100 cents

    // Filter: 0 off, 1 low-pass, 2 high-pass, 3 band-pass
    int   filterType   = 0;
    float cutoffHz     = 20000.0f;
    float resonance    = 0.0f;   // 0..1
    float filterEnvAmt = 0.0f;   // -1..1: how far the filter envelope moves the cutoff

    // Amp envelope and filter envelope: attack, decay and release in ms, sustain 0..1. The defaults are a flat gate.
    float ampA = 0.0f, ampD = 100.0f, ampS = 1.0f, ampR = 0.0f;
    float fltA = 0.0f, fltD = 100.0f, fltS = 1.0f, fltR = 0.0f;

    // Play
    bool  loop      = false;    // repeats the slice while the block lasts
    bool  oneShot   = true;     // plays to its end (true) or follows the block's length as a gate (false)
    int   chokeGroup = 0;       // 0 = none; a slice cuts any sounding slice of the same group

};

} // namespace mu_tilate
