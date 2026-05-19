#pragma once

// #434: single declarative table for per-rhythm APVTS parameters.
//
// Before this header, renaming a single param ID (e.g. `drvChar` → `insChar`)
// required updating five stringly-typed sites in lock-step: the APVTS
// declaration, the kRhythmSuffixes table, applyRhythmSuffix's if/else chain,
// pushRhythmToAPVTS's set() chain, and any UI binding. No compile-time check
// kept them aligned.
//
// This table pairs each suffix with the (rhythm → APVTS value) and
// (APVTS value → rhythm) transformations so they stay co-located. The
// previously-separate kRhythmSuffixes list now derives from this table by
// iteration; applyRhythmSuffix and pushRhythmToAPVTS both consume it.
//
// Each entry's value semantics match the prior hand-written code exactly:
// integer suffixes round through `(int)v`, ADSR sustains scale 0..100 ↔ 0..1
// via adsrSus(), `aEnvRel` retains its `>= 10.0f` end-mode sentinel (#420 /
// #217), the `drvChar` clamp respects the post-#422/#423 0..12 range, etc.
//
// Adding a new per-rhythm param now means: add one entry here + (separately)
// add one line in createParameterLayout that mirrors the suffix + range.
// Skipping either is impossible to miss — every save/load/sync path iterates
// this single table.

#include <juce_core/juce_core.h>
#include "Sequencer/Rhythm.h"

namespace mu_pp {

// Convert 0–100 UI scale → 0–1 amplitude for ADSR Sustain. Mirrors the helper
// declared at the top of PluginProcessor_Internal.h; duplicated here so this
// header is self-contained.
inline float adsrSusLocal(float v) noexcept { return juce::jlimit(0.0f, 1.0f, v / 100.0f); }

struct RhythmParamDef
{
    const char* suffix;
    // Apply a display-scale value to a Rhythm. Sets patternDirty when the
    // change requires updatePattern(); sets voiceDirty when it requires
    // syncing voiceParams to the VoiceEngine.
    void  (*apply)(float v, Rhythm& r, bool& patternDirty, bool& voiceDirty);
    // Read the current display-scale value from a Rhythm. Used by
    // pushRhythmToAPVTS to mirror the in-memory state to APVTS after a
    // hot-swap commit / preset load.
    float (*push)(const Rhythm& r);
};

// Expand all nine HitGen suffixes for one letter (A / B / C). Suffixes:
// stepsX, hitsX, rotX, prePadX, postPadX, insStX, insLenX, insModeX,
// prePadModeX, postPadModeX. Insert / pad MODE bools encode Mute=1 / Pad=0.
#define MU_HITGEN_ENTRIES(L)                                                                                                                                                       \
    { "steps" #L,       [](float v, Rhythm& r, bool& pd, bool&) { r.gen##L.steps        = juce::jlimit(1, 64, (int)v); pd = true; },                                                \
                        [](const Rhythm& r) -> float { return (float) r.gen##L.steps; } },                                                                                          \
    { "hits"  #L,       [](float v, Rhythm& r, bool& pd, bool&) { r.gen##L.hits         = juce::jlimit(0, 64, (int)v); pd = true; },                                                \
                        [](const Rhythm& r) -> float { return (float) r.gen##L.hits; } },                                                                                           \
    { "rot"   #L,       [](float v, Rhythm& r, bool& pd, bool&) { r.gen##L.rotate       = (int)v;                      pd = true; },                                                \
                        [](const Rhythm& r) -> float { return (float) r.gen##L.rotate; } },                                                                                         \
    { "prePad" #L,      [](float v, Rhythm& r, bool& pd, bool&) { r.gen##L.prePad       = juce::jlimit(0, 12, (int)v); pd = true; },                                                \
                        [](const Rhythm& r) -> float { return (float) r.gen##L.prePad; } },                                                                                         \
    { "postPad" #L,     [](float v, Rhythm& r, bool& pd, bool&) { r.gen##L.postPad      = juce::jlimit(0, 12, (int)v); pd = true; },                                                \
                        [](const Rhythm& r) -> float { return (float) r.gen##L.postPad; } },                                                                                        \
    { "insSt" #L,       [](float v, Rhythm& r, bool& pd, bool&) { r.gen##L.insertStart  = juce::jlimit(0, 63, (int)v); pd = true; },                                                \
                        [](const Rhythm& r) -> float { return (float) r.gen##L.insertStart; } },                                                                                    \
    { "insLen" #L,      [](float v, Rhythm& r, bool& pd, bool&) { r.gen##L.insertLength = juce::jlimit(0,  8, (int)v); pd = true; },                                                \
                        [](const Rhythm& r) -> float { return (float) r.gen##L.insertLength; } },                                                                                   \
    { "insMode" #L,     [](float v, Rhythm& r, bool& pd, bool&) { r.gen##L.insertMode   = v > 0.5f ? InsertMode::Mute : InsertMode::Pad; pd = true; },                              \
                        [](const Rhythm& r) -> float { return r.gen##L.insertMode  == InsertMode::Mute ? 1.0f : 0.0f; } },                                                          \
    { "prePadMode" #L,  [](float v, Rhythm& r, bool& pd, bool&) { r.gen##L.prePadMode   = v > 0.5f ? InsertMode::Mute : InsertMode::Pad; pd = true; },                              \
                        [](const Rhythm& r) -> float { return r.gen##L.prePadMode  == InsertMode::Mute ? 1.0f : 0.0f; } },                                                          \
    { "postPadMode" #L, [](float v, Rhythm& r, bool& pd, bool&) { r.gen##L.postPadMode  = v > 0.5f ? InsertMode::Mute : InsertMode::Pad; pd = true; },                              \
                        [](const Rhythm& r) -> float { return r.gen##L.postPadMode == InsertMode::Mute ? 1.0f : 0.0f; } }

inline const RhythmParamDef kRhythmParamDefs[] = {
    // ── HitGen A / B / C ─────────────────────────────────────────────────────
    MU_HITGEN_ENTRIES(A),
    MU_HITGEN_ENTRIES(B),
    MU_HITGEN_ENTRIES(C),

    // ── Rhythm-level (sequencer-side, not voiceParams) ───────────────────────
    { "logic",     [](float v, Rhythm& r, bool& pd, bool&)  { r.logic = static_cast<Logic>(juce::jlimit(0, 4, (int)v)); pd = true; },
                   [](const Rhythm& r) -> float { return (float) r.logic; } },
    // #419 patternLegato is sequencer-level — no engine sync needed.
    { "patLeg",    [](float v, Rhythm& r, bool&, bool&)     { r.patternLegato = (v > 0.5f); },
                   [](const Rhythm& r) -> float { return r.patternLegato ? 1.0f : 0.0f; } },

    // ── Pitch ────────────────────────────────────────────────────────────────
    { "pitchOct",  [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.pitchOctave    = juce::jlimit(-4,  4, (int)v); vd = true; },
                   [](const Rhythm& r) -> float { return (float) r.voiceParams.pitchOctave; } },
    { "pitchSemi", [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.pitchSemitones = juce::jlimit(-12, 12, (int)v); vd = true; },
                   [](const Rhythm& r) -> float { return (float) r.voiceParams.pitchSemitones; } },
    { "pitchFine", [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.pitchFine      = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.pitchFine; } },

    // ── Pitch envelope ───────────────────────────────────────────────────────
    // #287 — pEnv times stored in seconds directly (0..10 s, skew 0.3).
    // #420: ADSR-time floor is applied at the JUCE ADSR boundary inside
    // VoiceEngine::syncEnvelopes, NOT here, so voiceParams faithfully holds the
    // user's actual knob value (a 0 round-trips as 0, not as 0.001).
    { "pEnvAtk",   [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.pitchEnvAtk    = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.pitchEnvAtk; } },
    { "pEnvDec",   [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.pitchEnvDec    = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.pitchEnvDec; } },
    { "pEnvSus",   [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.pitchEnvSus    = adsrSusLocal(v); vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.pitchEnvSus * 100.0f; } },
    { "pEnvRel",   [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.pitchEnvRel    = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.pitchEnvRel; } },
    { "pEnvDep",   [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.pitchEnvDepth  = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.pitchEnvDepth; } },
    // #221 per-envelope legato (skip reset before noteOn so retriggers don't click).
    { "pEnvLeg",   [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.pitchEnvLegato = (v > 0.5f); vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.pitchEnvLegato ? 1.0f : 0.0f; } },

    // ── Filter + filter envelope ─────────────────────────────────────────────
    { "fltType",   [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.filterType   = juce::jlimit(0, 15, (int)v); vd = true; },
                   [](const Rhythm& r) -> float { return (float) r.voiceParams.filterType; } },
    { "fltCut",    [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.filterCutoff = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.filterCutoff; } },
    { "fltRes",    [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.filterRes    = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.filterRes; } },
    // #286 — fEnv times in seconds directly.
    { "fEnvAtk",   [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.filterEnvAtk = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.filterEnvAtk; } },
    { "fEnvDec",   [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.filterEnvDec = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.filterEnvDec; } },
    { "fEnvSus",   [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.filterEnvSus = adsrSusLocal(v); vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.filterEnvSus * 100.0f; } },
    { "fEnvRel",   [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.filterEnvRel = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.filterEnvRel; } },
    { "fEnvDep",   [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.filterEnvDepth = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.filterEnvDepth; } },
    { "fEnvLeg",   [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.filterEnvLegato = (v > 0.5f); vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.filterEnvLegato ? 1.0f : 0.0f; } },

    // ── Amp + amp envelope ───────────────────────────────────────────────────
    { "ampLvl",    [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.ampLevel    = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.ampLevel; } },
    { "aEnvAtk",   [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.ampEnvAtk   = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.ampEnvAtk; } },
    { "aEnvDec",   [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.ampEnvDec   = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.ampEnvDec; } },
    { "aEnvSus",   [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.ampEnvSus   = adsrSusLocal(v); vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.ampEnvSus * 100.0f; } },
    // #217: aEnvRel keeps the `>= 10.0f` end-mode sentinel — at that value the
    // amp envelope's release doesn't fade the sample; the sample plays through
    // to its natural end (ampRelToEnd). Both apply and push round-trip this.
    { "aEnvRel",   [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.ampEnvRel = v; r.voiceParams.ampRelToEnd = (v >= 10.0f); vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.ampRelToEnd ? 10.0f : r.voiceParams.ampEnvRel; } },
    { "aEnvLeg",   [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.ampEnvLegato = (v > 0.5f); vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.ampEnvLegato ? 1.0f : 0.0f; } },
    { "accentDb",  [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.accentDb = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.accentDb; } },

    // ── Insert / drive (post-#422/#423 algorithm range 0..12) ────────────────
    { "drvChar",   [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.driveChar  = juce::jlimit(0, 12, (int)v); vd = true; },
                   [](const Rhythm& r) -> float { return (float) r.voiceParams.driveChar; } },
    { "drvDrv",    [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.driveDrive  = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.driveDrive; } },
    { "drvOut",    [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.driveOutput = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.driveOutput; } },
    { "drvBits",   [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.drvBits     = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.drvBits; } },
    { "drvRate",   [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.driveRate   = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.driveRate; } },
    { "drvDit",    [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.drvDither   = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.drvDither; } },
    { "drvTon",    [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.driveTone   = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.driveTone; } },
    { "eqMidGain", [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.eqMidGain   = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.eqMidGain; } },
};

inline constexpr int kRhythmParamCount = (int)(sizeof(kRhythmParamDefs) / sizeof(kRhythmParamDefs[0]));

// Linear lookup by suffix. ~50 entries × strcmp is fast enough for the
// interactive paths that call it (parameterChanged, preset load), and avoids
// the static-init order issue of a std::unordered_map.
inline const RhythmParamDef* findRhythmParamDef(const juce::String& suffix) noexcept
{
    for (int i = 0; i < kRhythmParamCount; ++i)
        if (suffix == kRhythmParamDefs[i].suffix)
            return &kRhythmParamDefs[i];
    return nullptr;
}

#undef MU_HITGEN_ENTRIES

} // namespace mu_pp
