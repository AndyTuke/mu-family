#pragma once

// single declarative table for per-rhythm APVTS parameters.
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
// via adsrSus(), `aEnvRel` retains its `>= 10.0f` end-mode sentinel,
// the `drvChar` clamp derives from kInsertAlgorithmNames count, etc.
//
// Adding a new per-rhythm param now means: add one entry here + (separately)
// add one line in createParameterLayout that mirrors the suffix + range.
// Skipping either is impossible to miss — every save/load/sync path iterates
// this single table.

#include <juce_core/juce_core.h>
#include "Sequencer/Rhythm.h"
#include "Audio/AlgorithmNames.h"   // Stage 35: kInsertAlgorithmNames / kFilterTypeNames

namespace mu_pp {

// Convert 0–100 UI scale → 0–1 amplitude for ADSR Sustain. Mirrors the helper
// declared at the top of PluginProcessor_Internal.h; duplicated here so this
// header is self-contained.
inline float adsrSusLocal(float v) noexcept { return juce::jlimit(0.0f, 1.0f, v / 100.0f); }

// Stage 35 Step 1: per-param type tag drives the v2 preset format. Apply / push
// always operate in display-scale float — this just controls how the value is
// serialised to / parsed from XML and (for AlgorithmIndex kinds) which name
// table is used to map back and forth to a stable string ID.
enum class ParamKind
{
    Float,            // generic continuous value; written as XML float
    Int,              // integer; written as XML int (steps, pitch octave, etc.)
    Bool,             // boolean; written as XML "true" / "false"
    AlgorithmIndex,   // categorical; written as the stable name from algorithmNames
};

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
    // Stage 35: param type + algorithm name table (for AlgorithmIndex kinds).
    // Default Float/nullptr keeps existing entries working until we backfill
    // the right kind below.
    ParamKind          kind            = ParamKind::Float;
    const char* const* algorithmNames  = nullptr;
};

// Expand all nine HitGen suffixes for one letter (A / B / C). Suffixes:
// stepsX, hitsX, rotX, prePadX, postPadX, insStX, insLenX, insModeX,
// prePadModeX, postPadModeX. Insert / pad MODE bools encode Mute=1 / Pad=0.
// Stage 35: kind tags drive the v2 preset format — integer counts become
// `<r0_stepsA value="16"/>`, mode bools become `value="true"/"false"`.
#define MU_HITGEN_ENTRIES(L)                                                                                                                                                       \
    { "steps" #L,       [](float v, Rhythm& r, bool& pd, bool&) { r.gen##L.steps        = juce::jlimit(1, 64, (int)v); pd = true; },                                                \
                        [](const Rhythm& r) -> float { return (float) r.gen##L.steps; },        ParamKind::Int },                                                                   \
    { "hits"  #L,       [](float v, Rhythm& r, bool& pd, bool&) { r.gen##L.hits         = juce::jlimit(0, 64, (int)v); pd = true; },                                                \
                        [](const Rhythm& r) -> float { return (float) r.gen##L.hits; },         ParamKind::Int },                                                                   \
    { "rot"   #L,       [](float v, Rhythm& r, bool& pd, bool&) { r.gen##L.rotate       = (int)v;                      pd = true; },                                                \
                        [](const Rhythm& r) -> float { return (float) r.gen##L.rotate; },       ParamKind::Int },                                                                   \
    { "prePad" #L,      [](float v, Rhythm& r, bool& pd, bool&) { r.gen##L.prePad       = juce::jlimit(0, 12, (int)v); pd = true; },                                                \
                        [](const Rhythm& r) -> float { return (float) r.gen##L.prePad; },       ParamKind::Int },                                                                   \
    { "postPad" #L,     [](float v, Rhythm& r, bool& pd, bool&) { r.gen##L.postPad      = juce::jlimit(0, 12, (int)v); pd = true; },                                                \
                        [](const Rhythm& r) -> float { return (float) r.gen##L.postPad; },      ParamKind::Int },                                                                   \
    { "insSt" #L,       [](float v, Rhythm& r, bool& pd, bool&) { r.gen##L.insertStart  = juce::jlimit(0, 63, (int)v); pd = true; },                                                \
                        [](const Rhythm& r) -> float { return (float) r.gen##L.insertStart; },  ParamKind::Int },                                                                   \
    { "insLen" #L,      [](float v, Rhythm& r, bool& pd, bool&) { r.gen##L.insertLength = juce::jlimit(0,  8, (int)v); pd = true; },                                                \
                        [](const Rhythm& r) -> float { return (float) r.gen##L.insertLength; }, ParamKind::Int },                                                                   \
    { "insMode" #L,     [](float v, Rhythm& r, bool& pd, bool&) { r.gen##L.insertMode   = v > 0.5f ? InsertMode::Mute : InsertMode::Pad; pd = true; },                              \
                        [](const Rhythm& r) -> float { return r.gen##L.insertMode  == InsertMode::Mute ? 1.0f : 0.0f; }, ParamKind::Bool },                                         \
    { "prePadMode" #L,  [](float v, Rhythm& r, bool& pd, bool&) { r.gen##L.prePadMode   = v > 0.5f ? InsertMode::Mute : InsertMode::Pad; pd = true; },                              \
                        [](const Rhythm& r) -> float { return r.gen##L.prePadMode  == InsertMode::Mute ? 1.0f : 0.0f; }, ParamKind::Bool },                                         \
    { "postPadMode" #L, [](float v, Rhythm& r, bool& pd, bool&) { r.gen##L.postPadMode  = v > 0.5f ? InsertMode::Mute : InsertMode::Pad; pd = true; },                              \
                        [](const Rhythm& r) -> float { return r.gen##L.postPadMode == InsertMode::Mute ? 1.0f : 0.0f; }, ParamKind::Bool }

inline const RhythmParamDef kRhythmParamDefs[] = {
    // ── HitGen A / B / C ─────────────────────────────────────────────────────
    MU_HITGEN_ENTRIES(A),
    MU_HITGEN_ENTRIES(B),
    MU_HITGEN_ENTRIES(C),

    // ── Rhythm-level (sequencer-side, not voiceParams) ───────────────────────
    // Logic is an algorithm-style enumerator (OR / AND / XOR / AOnly /
    // BOnly) — write as the stable name string so reordering or inserting
    // new logic modes doesn't shift saved presets.
    { "logic",     [](float v, Rhythm& r, bool& pd, bool&)  { r.logic = static_cast<Logic>(juce::jlimit(0, 4, (int)v)); pd = true; },
                   [](const Rhythm& r) -> float { return (float) r.logic; },
                   ParamKind::AlgorithmIndex, mu_audio::kLogicNames },
    // patternLegato is sequencer-level — no engine sync needed.
    { "patLeg",    [](float v, Rhythm& r, bool&, bool&)     { r.patternLegato = (v > 0.5f); },
                   [](const Rhythm& r) -> float { return r.patternLegato ? 1.0f : 0.0f; },  ParamKind::Bool },
    // resetSteps: -1 → nullopt (free-running); 1..256 → fixed cycle length.
    { "rstSt",     [](float v, Rhythm& r, bool& pd, bool&)  {
                       const int n = (int)v;
                       r.resetSteps = (n < 0) ? std::nullopt : std::optional<int>(juce::jlimit(1, 256, n));
                       pd = true;
                   },
                   [](const Rhythm& r) -> float { return (float) r.resetSteps.value_or(-1); }, ParamKind::Int },

    // ── Pitch ────────────────────────────────────────────────────────────────
    { "pitchOct",  [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.pitchOctave    = juce::jlimit(-4,  4, (int)v); vd = true; },
                   [](const Rhythm& r) -> float { return (float) r.voiceParams.pitchOctave; },    ParamKind::Int },
    { "pitchSemi", [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.pitchSemitones = juce::jlimit(-12, 12, (int)v); vd = true; },
                   [](const Rhythm& r) -> float { return (float) r.voiceParams.pitchSemitones; }, ParamKind::Int },
    { "pitchFine", [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.pitchFine      = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.pitchFine; } },

    // ── Pitch envelope ───────────────────────────────────────────────────────
    // pEnv times stored in seconds directly (0..10 s, skew 0.3).
    // ADSR-time floor is applied at the JUCE ADSR boundary inside
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
    // per-envelope legato (skip reset before noteOn so retriggers don't click).
    { "pEnvLeg",   [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.pitchEnvLegato = (v > 0.5f); vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.pitchEnvLegato ? 1.0f : 0.0f; },  ParamKind::Bool },

    // ── Filter + filter envelope ─────────────────────────────────────────────
    // Stage 35: fltType is an algorithm selector → string name in v2 preset XML.
    { "fltType",   [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.filterType   = juce::jlimit(0, 15, (int)v); vd = true; },
                   [](const Rhythm& r) -> float { return (float) r.voiceParams.filterType; },
                   ParamKind::AlgorithmIndex, mu_audio::kFilterTypeNames },
    { "fltCut",    [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.filterCutoff = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.filterCutoff; } },
    { "fltRes",    [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.filterRes    = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.filterRes; } },
    // fEnv times in seconds directly.
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
                   [](const Rhythm& r) -> float { return r.voiceParams.filterEnvLegato ? 1.0f : 0.0f; },  ParamKind::Bool },
    { "fltLoCut",  [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.filterLowCutHz = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.filterLowCutHz; } },

    // ── Amp + amp envelope ───────────────────────────────────────────────────
    { "ampLvl",    [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.ampLevel    = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.ampLevel; } },
    { "aEnvAtk",   [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.ampEnvAtk   = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.ampEnvAtk; } },
    { "aEnvDec",   [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.ampEnvDec   = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.ampEnvDec; } },
    { "aEnvSus",   [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.ampEnvSus   = adsrSusLocal(v); vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.ampEnvSus * 100.0f; } },
    // aEnvRel keeps the `>= 10.0f` end-mode sentinel — at that value the
    // amp envelope's release doesn't fade the sample; the sample plays through
    // to its natural end (ampRelToEnd). Both apply and push round-trip this.
    { "aEnvRel",   [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.ampEnvRel = v; r.voiceParams.ampRelToEnd = (v >= 10.0f); vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.ampRelToEnd ? 10.0f : r.voiceParams.ampEnvRel; } },
    { "aEnvLeg",   [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.ampEnvLegato = (v > 0.5f); vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.ampEnvLegato ? 1.0f : 0.0f; },  ParamKind::Bool },
    { "accentDb",  [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.accentDb = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.accentDb; } },

    // ── Insert / drive ────────────────────────────────────────────────────────
    // Stage 35: drvChar is an algorithm selector → string name in v2 preset XML.
    // Upper bound is derived from kInsertAlgorithmNames so it auto-tracks new entries.
    { "drvChar",   [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.insertAlgo  = juce::jlimit(0, mu_audio::countNames(mu_audio::kInsertAlgorithmNames) - 1, (int)v); vd = true; },
                   [](const Rhythm& r) -> float { return (float) r.voiceParams.insertAlgo; },
                   ParamKind::AlgorithmIndex, mu_audio::kInsertAlgorithmNames },
    { "drvDrv",    [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.insertDrive  = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.insertDrive; } },
    { "drvOut",    [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.insertOutput = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.insertOutput; } },
    { "drvBits",   [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.insertBits     = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.insertBits; } },
    { "drvRate",   [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.insertRate   = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.insertRate; } },
    { "drvDit",    [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.insertDither   = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.insertDither; } },
    { "drvTon",    [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.insertTone   = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.insertTone; } },
    { "eqMidGain", [](float v, Rhythm& r, bool&, bool& vd)  { r.voiceParams.insertEqMid   = v; vd = true; },
                   [](const Rhythm& r) -> float { return r.voiceParams.insertEqMid; } },
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
