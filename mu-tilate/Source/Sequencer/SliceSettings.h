#pragma once

// What one slice plays like (docs/mu-tilate/design-ui.md, "Slice player"): the settings the Slice view edits for the selected
// slice or hit. Every value is an OFFSET added to the layer's own (absolute) value of the same control, so the defaults are all
// zero and a slice with default settings plays as the layer says. Kept with the slice's marker (or on the hit), plain values, saved
// with the session.
#include <algorithm>
#include <cmath>

namespace mu_tilate
{

struct SliceSettings
{
    // Slice
    bool  reverse   = false;    // plays backwards; a timeline block that is reversed too cancels it out
    float levelDb   = 0.0f;     // -12..+12 dB, added to the layer's level (sends stay the layer's)
    float pan       = 0.0f;     // -1..1 added to the layer's pan
    float fadeInMs  = 0.0f;     // -25..25 ms added to the layer's fade in
    float fadeOutMs = 0.0f;     // -25..25 ms added to the layer's fade out

    // Pre-fade: a quiet reversed copy of the start of the slice that swells into the hit, mixed over whatever plays before it.
    bool  preFade     = false;
    float preLevelDb  = -12.0f;   // -36..0 dB below the slice's own level
    float preLengthMs = 150.0f;   // 10..1000 ms, ending exactly where the slice starts
    float preTone     = 0.0f;     // -1 (darker) .. 1 (thinner)

    // Pitch
    float pitchSemi = 0.0f;     // -12..12 semitones added to the layer's
    float fineCents = 0.0f;     // -50..50 cents added to the layer's

    // Filter: the layer chooses the type, cutoff and resonance; a slice only moves them. The offsets are what is saved.
    float cutoffOct    = 0.0f;   // -4..4 octaves added to the layer's cutoff
    float resOffset    = 0.0f;   // -1..1 added to the layer's resonance
    float filterEnvAmt = 0.0f;   // -1..1: how far the filter envelope moves the cutoff
    // The filter as the player runs it: the layer's setting with this slice's offsets applied. The scheduler fills these in
    // when it starts a hit; they are not saved, and the pre-fade's tone filter writes them on its own voice. 0 off, 1 low-pass,
    // 2 high-pass, 3 band-pass.
    int   filterType   = 0;
    float cutoffHz     = 20000.0f;
    float resonance    = 0.0f;   // 0..1

    // Amp envelope and filter envelope: attack, decay and release in ms (-1000..1000) and sustain (-1..1), each added to the
    // layer's value (the sum is held to the control's own range).
    float ampA = 0.0f, ampD = 0.0f, ampS = 0.0f, ampR = 0.0f;
    float fltA = 0.0f, fltD = 0.0f, fltS = 0.0f, fltR = 0.0f;

    // The layer's insert effect: how far this slice moves each of its four knobs, as a fraction of the knob's travel (-1..1).
    float insOff[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    // The layer's three FX sends (Effect, Delay, Reverb): how far this slice moves each, added to the strip's send for as long as the
    // slice sounds (-1..1, the sum is held to 0..1). The mixer's own send knobs do not move.
    float sendOff[3] = { 0.0f, 0.0f, 0.0f };

    // Play
    bool  oneShot   = true;     // plays to its end (true) or follows the block's length as a gate (false)
    int   chokeGroup = 0;       // 0 = none; a slice cuts any sounding slice of the same group

    // This hit's own trim and retime, set by dragging the block's edges on the timeline (absolute, not offsets).
    float trimInBeats = 0.0f;   // >= 0: beats of the slice (at the loop's tempo) skipped at its start
    float retime      = 1.0f;   // 0.25..4: the hit plays in this many times its natural length (2 = twice as long); pitch follows the layer's Pitch/Stretch knob
    float volume      = 1.0f;   // 0..1: this hit's level as a fraction of full, set by dragging the top of the block on the timeline (multiplies the gain)
    float envIn       = 0.0f;   // 0..1: the hit's fade-in as a fraction of its length, set by dragging the block's top-left corner in
    float envOut      = 0.0f;   // 0..1: the fade-out as a fraction of its length, set by dragging the top-right corner in (envIn + envOut <= 1)
    float envInCurve  = 0.0f;   // -1..1: how the fade-in bends (0 straight, above 0 slow start, below 0 fast start), the handle in the middle of the fade
    float envOutCurve = 0.0f;   // -1..1: the same for the fade-out

};

// Brings every setting inside its documented range and replaces NaN or infinity with the default: a saved file or a host can
// hand over anything, and a NaN cutoff would turn the filter's state, and then the output, into NaN.
inline SliceSettings sanitised(SliceSettings s) noexcept
{
    const SliceSettings d;
    auto fix = [](float v, float lo, float hi, float def) { return std::isfinite(v) ? std::clamp(v, lo, hi) : def; };
    s.levelDb = fix(s.levelDb, -12.0f, 12.0f, d.levelDb);
    s.pan = fix(s.pan, -1.0f, 1.0f, d.pan);
    s.fadeInMs = fix(s.fadeInMs, -25.0f, 25.0f, d.fadeInMs);
    s.fadeOutMs = fix(s.fadeOutMs, -25.0f, 25.0f, d.fadeOutMs);
    s.preLevelDb = fix(s.preLevelDb, -36.0f, 0.0f, d.preLevelDb);
    s.preLengthMs = fix(s.preLengthMs, 10.0f, 1000.0f, d.preLengthMs);
    s.preTone = fix(s.preTone, -1.0f, 1.0f, d.preTone);
    s.pitchSemi = fix(s.pitchSemi, -12.0f, 12.0f, d.pitchSemi);
    s.fineCents = fix(s.fineCents, -50.0f, 50.0f, d.fineCents);
    s.cutoffOct = fix(s.cutoffOct, -4.0f, 4.0f, d.cutoffOct);
    s.resOffset = fix(s.resOffset, -1.0f, 1.0f, d.resOffset);
    s.filterType = std::clamp(s.filterType, 0, 3);
    s.cutoffHz = fix(s.cutoffHz, 20.0f, 20000.0f, d.cutoffHz);
    s.resonance = fix(s.resonance, 0.0f, 1.0f, d.resonance);
    s.filterEnvAmt = fix(s.filterEnvAmt, -1.0f, 1.0f, d.filterEnvAmt);
    s.ampA = fix(s.ampA, -1000.0f, 1000.0f, d.ampA);   s.ampD = fix(s.ampD, -1000.0f, 1000.0f, d.ampD);
    s.ampS = fix(s.ampS, -1.0f, 1.0f, d.ampS);         s.ampR = fix(s.ampR, -1000.0f, 1000.0f, d.ampR);
    s.fltA = fix(s.fltA, -1000.0f, 1000.0f, d.fltA);   s.fltD = fix(s.fltD, -1000.0f, 1000.0f, d.fltD);
    s.fltS = fix(s.fltS, -1.0f, 1.0f, d.fltS);         s.fltR = fix(s.fltR, -1000.0f, 1000.0f, d.fltR);
    for (auto& o : s.insOff) o = fix(o, -1.0f, 1.0f, 0.0f);
    for (auto& o : s.sendOff) o = fix(o, -1.0f, 1.0f, 0.0f);
    s.chokeGroup = std::clamp(s.chokeGroup, 0, 8);
    s.trimInBeats = fix(s.trimInBeats, 0.0f, 4096.0f, 0.0f);
    s.retime = fix(s.retime, 0.25f, 4.0f, 1.0f);
    s.volume = fix(s.volume, 0.0f, 1.0f, 1.0f);
    s.envIn = fix(s.envIn, 0.0f, 1.0f, 0.0f);
    s.envOut = std::min(fix(s.envOut, 0.0f, 1.0f, 0.0f), 1.0f - s.envIn);
    s.envInCurve = fix(s.envInCurve, -1.0f, 1.0f, 0.0f);
    s.envOutCurve = fix(s.envOutCurve, -1.0f, 1.0f, 0.0f);
    return s;
}

// A fade's bend (-1..1) as the exponent of its ramp: the gain is x^exponent for x rising 0..1 along the fade. 0 is a straight line, 1 is
// x^4 (slow start), -1 is x^0.25 (fast start).
inline float envCurveExponent(float curve) noexcept { return std::pow(4.0f, std::clamp(curve, -1.0f, 1.0f)); }

// The pitch settings as a rate: source samples per output sample (1 = unchanged), semitones and cents clamped to their ranges.
inline double pitchRatio(const SliceSettings& s) noexcept
{
    const double semis = std::clamp((double) s.pitchSemi, -24.0, 24.0) + std::clamp((double) s.fineCents, -100.0, 100.0) / 100.0;
    return std::exp2(semis / 12.0);
}

} // namespace mu_tilate
