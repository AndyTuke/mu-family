#pragma once

// What one slice plays like (docs/mu-tilate/design-ui.md, "Slice player"): the settings the Slice view edits for the selected
// slice or hit. Every value is an OFFSET added to the layer's own (absolute) value of the same control, so the defaults are all
// zero and a slice with default settings plays as the layer says. Kept with the slice's marker (or on the hit), plain values, saved
// with the session.
#include <algorithm>
#include <cmath>

namespace mu_tilate
{

// The knobs of the Slice view that can hold an absolute value instead of an offset from the layer's own value, one per knob (the
// order is the saved order of nothing: each is saved under its own key).
enum class OverrideField : int
{
    PitchSemi, FineCents, CutoffOct, ResOffset, FilterEnvAmt, FltA, FltD, FltS, FltR,
    LevelDb, Pan, FadeIn, FadeOut, AmpA, AmpD, AmpS, AmpR,
    Ins0, Ins1, Ins2, Ins3, Send0, Send1, Send2,
    Count
};
inline constexpr int kOverrideFieldCount = (int) OverrideField::Count;

// One knob's choice: Offset (the default; the knob's value is the offset member of the settings) or Absolute, where the knob sets the
// parameter's actual value for this slice or hit and the layer's own value is ignored. The offset is left alone while the knob is
// Absolute, so switching back finds it where it was; Clear puts both back to nothing.
struct FieldOverride
{
    bool  absolute = false;
    float absValue = 0.0f;   // the actual value, in the control's own unit (cutoff in Hz, level in dB, ...)
};

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

    // Which knobs are Absolute (see FieldOverride); every other knob is the offset member above.
    FieldOverride ov[kOverrideFieldCount];
};

// The offset member a knob edits.
inline float& offsetRef(SliceSettings& s, OverrideField f) noexcept
{
    switch (f)
    {
        case OverrideField::PitchSemi:    return s.pitchSemi;
        case OverrideField::FineCents:    return s.fineCents;
        case OverrideField::CutoffOct:    return s.cutoffOct;
        case OverrideField::ResOffset:    return s.resOffset;
        case OverrideField::FilterEnvAmt: return s.filterEnvAmt;
        case OverrideField::FltA:         return s.fltA;
        case OverrideField::FltD:         return s.fltD;
        case OverrideField::FltS:         return s.fltS;
        case OverrideField::FltR:         return s.fltR;
        case OverrideField::LevelDb:      return s.levelDb;
        case OverrideField::Pan:          return s.pan;
        case OverrideField::FadeIn:       return s.fadeInMs;
        case OverrideField::FadeOut:      return s.fadeOutMs;
        case OverrideField::AmpA:         return s.ampA;
        case OverrideField::AmpD:         return s.ampD;
        case OverrideField::AmpS:         return s.ampS;
        case OverrideField::AmpR:         return s.ampR;
        case OverrideField::Ins0:         return s.insOff[0];
        case OverrideField::Ins1:         return s.insOff[1];
        case OverrideField::Ins2:         return s.insOff[2];
        case OverrideField::Ins3:         return s.insOff[3];
        case OverrideField::Send0:        return s.sendOff[0];
        case OverrideField::Send1:        return s.sendOff[1];
        case OverrideField::Send2:        return s.sendOff[2];
        case OverrideField::Count:        break;
    }
    return s.pitchSemi;   // not reached
}
inline float offsetOf(const SliceSettings& s, OverrideField f) noexcept { return offsetRef(const_cast<SliceSettings&>(s), f); }

// The layer's own values of the overridable knobs, in the knobs' absolute units (level in dB, cutoff in Hz, the insert knobs 0..1,
// the sends 0..1). The Slice view reads them to jump a knob to what it sounds like now; the player resolves with the same numbers.
struct LayerValues
{
    float pitchSemi = 0.0f, fineCents = 0.0f, cutoffHz = 20000.0f, resonance = 0.0f, filterEnvAmt = 0.0f;
    float fltA = 0.0f, fltD = 0.0f, fltS = 1.0f, fltR = 0.0f;
    float levelDb = 0.0f, pan = 0.0f, fadeInMs = 0.0f, fadeOutMs = 0.0f;
    float ampA = 0.0f, ampD = 0.0f, ampS = 1.0f, ampR = 0.0f;
    float ins[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float send[3] = { 0.0f, 0.0f, 0.0f };
};

inline float layerValueOf(const LayerValues& v, OverrideField f) noexcept
{
    switch (f)
    {
        case OverrideField::PitchSemi:    return v.pitchSemi;
        case OverrideField::FineCents:    return v.fineCents;
        case OverrideField::CutoffOct:    return v.cutoffHz;
        case OverrideField::ResOffset:    return v.resonance;
        case OverrideField::FilterEnvAmt: return v.filterEnvAmt;
        case OverrideField::FltA:         return v.fltA;
        case OverrideField::FltD:         return v.fltD;
        case OverrideField::FltS:         return v.fltS;
        case OverrideField::FltR:         return v.fltR;
        case OverrideField::LevelDb:      return v.levelDb;
        case OverrideField::Pan:          return v.pan;
        case OverrideField::FadeIn:       return v.fadeInMs;
        case OverrideField::FadeOut:      return v.fadeOutMs;
        case OverrideField::AmpA:         return v.ampA;
        case OverrideField::AmpD:         return v.ampD;
        case OverrideField::AmpS:         return v.ampS;
        case OverrideField::AmpR:         return v.ampR;
        case OverrideField::Ins0:         return v.ins[0];
        case OverrideField::Ins1:         return v.ins[1];
        case OverrideField::Ins2:         return v.ins[2];
        case OverrideField::Ins3:         return v.ins[3];
        case OverrideField::Send0:        return v.send[0];
        case OverrideField::Send1:        return v.send[1];
        case OverrideField::Send2:        return v.send[2];
        case OverrideField::Count:        break;
    }
    return 0.0f;
}

// What the knob sounds like for these settings against the layer's values, in the knob's absolute unit: the absolute value when the
// knob is Absolute, else the layer's value moved by the offset (the cutoff is the layer's Hz moved by octaves).
inline float effectiveValue(const SliceSettings& s, const LayerValues& layer, OverrideField f) noexcept
{
    const auto& o = s.ov[(int) f];
    if (o.absolute) return o.absValue;
    const float base = layerValueOf(layer, f), off = offsetOf(s, f);
    return f == OverrideField::CutoffOct ? base * std::exp2(off) : base + off;
}

// The offset a knob contributes to the player, given the layer's own value in the same unit: an Absolute knob replaces the layer's
// value, so its offset is the distance from it.
inline float resolvedOffset(const SliceSettings& s, OverrideField f, float layerValue) noexcept
{
    const auto& o = s.ov[(int) f];
    return o.absolute ? o.absValue - layerValue : offsetOf(s, f);
}

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
    for (auto& o : s.ov) o.absValue = std::isfinite(o.absValue) ? std::clamp(o.absValue, -1.0e5f, 1.0e5f) : 0.0f;
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
