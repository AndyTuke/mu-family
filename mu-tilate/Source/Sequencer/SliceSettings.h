#pragma once

// What one slice plays like (docs/mu-tilate/design-ui.md, "Slice player"): the settings the slice player panel edits for the
// selected slice, kept with the slice's marker so they follow it when markers are added, moved or removed. Plain values, saved
// with the session. The defaults are neutral: a slice with default settings plays as the source.
#include <algorithm>
#include <cmath>

namespace mu_tilate
{

struct SliceSettings
{
    // Slice
    bool  reverse   = false;    // plays backwards; a timeline block that is reversed too cancels it out
    float levelDb   = 0.0f;     // -24..+12 dB, added to the layer's level (sends stay the layer's)
    float pan       = 0.0f;     // -1 (left) .. 1 (right)
    float fadeInMs  = 0.0f;     // added to the layer's fade in
    float fadeOutMs = 0.0f;     // added to the layer's fade out

    // Pre-fade: a quiet reversed copy of the start of the slice that swells into the hit, mixed over whatever plays before it.
    bool  preFade     = false;
    float preLevelDb  = -12.0f;   // -36..0 dB below the slice's own level
    float preLengthMs = 150.0f;   // 10..1000 ms, ending exactly where the slice starts
    float preTone     = 0.0f;     // -1 (darker) .. 1 (thinner)

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

// Brings every setting inside its documented range and replaces NaN or infinity with the default: a saved file or a host can
// hand over anything, and a NaN cutoff would turn the filter's state, and then the output, into NaN.
inline SliceSettings sanitised(SliceSettings s) noexcept
{
    const SliceSettings d;
    auto fix = [](float v, float lo, float hi, float def) { return std::isfinite(v) ? std::clamp(v, lo, hi) : def; };
    s.levelDb = fix(s.levelDb, -24.0f, 12.0f, d.levelDb);
    s.pan = fix(s.pan, -1.0f, 1.0f, d.pan);
    s.fadeInMs = fix(s.fadeInMs, 0.0f, 1000.0f, d.fadeInMs);
    s.fadeOutMs = fix(s.fadeOutMs, 0.0f, 1000.0f, d.fadeOutMs);
    s.preLevelDb = fix(s.preLevelDb, -36.0f, 0.0f, d.preLevelDb);
    s.preLengthMs = fix(s.preLengthMs, 10.0f, 1000.0f, d.preLengthMs);
    s.preTone = fix(s.preTone, -1.0f, 1.0f, d.preTone);
    s.pitchSemi = fix(s.pitchSemi, -24.0f, 24.0f, d.pitchSemi);
    s.fineCents = fix(s.fineCents, -100.0f, 100.0f, d.fineCents);
    s.filterType = std::clamp(s.filterType, 0, 3);
    s.cutoffHz = fix(s.cutoffHz, 20.0f, 20000.0f, d.cutoffHz);
    s.resonance = fix(s.resonance, 0.0f, 1.0f, d.resonance);
    s.filterEnvAmt = fix(s.filterEnvAmt, -1.0f, 1.0f, d.filterEnvAmt);
    s.ampA = fix(s.ampA, 0.0f, 10000.0f, d.ampA);   s.ampD = fix(s.ampD, 0.0f, 10000.0f, d.ampD);
    s.ampS = fix(s.ampS, 0.0f, 1.0f, d.ampS);       s.ampR = fix(s.ampR, 0.0f, 10000.0f, d.ampR);
    s.fltA = fix(s.fltA, 0.0f, 10000.0f, d.fltA);   s.fltD = fix(s.fltD, 0.0f, 10000.0f, d.fltD);
    s.fltS = fix(s.fltS, 0.0f, 1.0f, d.fltS);       s.fltR = fix(s.fltR, 0.0f, 10000.0f, d.fltR);
    s.chokeGroup = std::clamp(s.chokeGroup, 0, 8);
    return s;
}

// The pitch settings as a rate: source samples per output sample (1 = unchanged), semitones and cents clamped to their ranges.
inline double pitchRatio(const SliceSettings& s) noexcept
{
    const double semis = std::clamp((double) s.pitchSemi, -24.0, 24.0) + std::clamp((double) s.fineCents, -100.0, 100.0) / 100.0;
    return std::exp2(semis / 12.0);
}

} // namespace mu_tilate
