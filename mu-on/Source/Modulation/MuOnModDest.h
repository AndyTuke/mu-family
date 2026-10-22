#pragma once

#include "UI/ModulatorEditor.h"            // mu-core: ModDestProvider
#include "UI/Components/DropdownSelect.h"   // mu-core
#include "Persistence/TrackParams.h"            // EngineType
#include "Modulation/ModTarget.h"         // mu-core: the standard target row
#include "UI/ModTargetDropdown.h"        // mu-core: the shared destination-dropdown builder

#include <cstddef>
#include <cstring>
#include <string>

// μ-On modulation-destination registry — one table per ENGINE (Kick / Bass / Hat / Snare / Rumble / FM Perc).
// Unlike mu-tant (every voice identical), μ-On's engines expose different params, so each engine has
// its own destination set; a track's set is the one of the engine it runs, and the editor swaps the
// ModulatorPanel's destination provider when the track's engine changes.
//
// Each entry carries the stable id saved in assignments (historically ending ".prop"), the
// parameter it drives (its id after the track prefix "t{N}_", so "kick_tune") and its dropdown
// label. Like every family target, depth is a percentage of the parameter's range: the engine seeds
// the slider's 0..1 proportion, the matrix offsets it, and resolveLane converts back via the
// param's NormalisableRange.
//
// The table ORDER is the single source of truth: it equals the order TrackEngine::setParams feeds
// the modulated values back into each engine's setParams(), so reordering an engine's table
// without matching the engine dispatch would scramble the mapping. New dests append.
namespace mu_on
{

// One row per target — the family-standard row (mu-core Modulation/ModTarget.h): id, dropdown
// label, section (none: each engine is its own list), parameter. Rows below are written
// { id, param, label } for readability, so they go through makeTarget.
using ModDestEntry = mu_mod::ModTarget;
constexpr ModDestEntry makeTarget(const char* id, const char* param, const char* label) { return { id, label, nullptr, param }; }

// Kick — order matches KickEngine::setParams(baseHz, pitchAmtHz, pitchDecMs, ampDecMs, drv).
inline constexpr ModDestEntry kKickDests[] = {
    makeTarget("k.tune.prop", "kick_tune", "Tune"),
    makeTarget("k.ptch.prop", "kick_ptch", "Pitch Amt"),
    makeTarget("k.pdec.prop", "kick_pdec", "Pitch Decay"),
    makeTarget("k.adec.prop", "kick_adec", "Decay"),
    makeTarget("k.drive.prop", "kick_drive", "Drive"),
};

// Bass — order matches BassEngine::setParams(rootHz, [wave], sub, cutHz, res, env,
// edecMs, atkMs, decMs, sus, drv). Wave is a choice param — not modulatable, omitted.
inline constexpr ModDestEntry kBassDests[] = {
    makeTarget("b.tune.prop", "bass_tune", "Tune"),
    makeTarget("b.sub.prop", "bass_sub", "Sub"),
    makeTarget("b.cut.prop", "bass_cut", "Cutoff"),
    makeTarget("b.res.prop", "bass_res", "Resonance"),
    makeTarget("b.env.prop", "bass_env", "Filter Env"),
    makeTarget("b.edec.prop", "bass_edec", "Env Decay"),
    makeTarget("b.atk.prop", "bass_atk", "Attack"),
    makeTarget("b.dec.prop", "bass_dec", "Decay"),
    makeTarget("b.sus.prop", "bass_sus", "Sustain"),
    makeTarget("b.drive.prop", "bass_drive", "Drive"),
};

// Hat — order matches SampleChannel::setParams(tuneSemitones, decayMs).
inline constexpr ModDestEntry kHatDests[] = {
    makeTarget("h.tune.prop", "hat_tune", "Tune"),
    makeTarget("h.dec.prop", "hat_dec", "Decay"),
};

// Snare — same SampleChannel::setParams order.
inline constexpr ModDestEntry kSnareDests[] = {
    makeTarget("s.tune.prop", "snare_tune", "Tune"),
    makeTarget("s.dec.prop", "snare_dec", "Decay"),
};

// Rumble — order matches RumbleEngine::setParams(bpm, drive, d1, d2, d3, revSize, revMix, revLpHz, cutHz, res, inLpHz)
// (bpm is the transport, not a dest — the 10 entries below are the modulatable args in order).
inline constexpr ModDestEntry kRumbleDests[] = {
    makeTarget("r.drive.prop", "rumble_drive", "Drive"),
    makeTarget("r.d1.prop", "rumble_d1", "1/16"),
    makeTarget("r.d2.prop", "rumble_d2", "2/16"),
    makeTarget("r.d3.prop", "rumble_d3", "3/16"),
    makeTarget("r.size.prop", "rumble_size", "Rev Size"),
    makeTarget("r.revmix.prop", "rumble_revmix", "Rev Mix"),
    makeTarget("r.revlp.prop", "rumble_revlp", "Rev LP"),
    makeTarget("r.cut.prop", "rumble_cut", "Cutoff"),
    makeTarget("r.res.prop", "rumble_res", "Resonance"),
    makeTarget("r.inlp.prop", "rumble_inlp", "Input LP"),   // appended last: saved destination indices stay valid
};

// FM Perc — order matches FmPercEngine::setParams(tune, ratio, index, idec, fdbk, dive, dtime, decay, fold, noise).
// The Algorithm is a choice, not a target. Every row but Tune and Ratio is a normalised amount, so a depth means the
// same musical share of the knob in every algorithm.
inline constexpr ModDestEntry kFmPercDests[] = {
    makeTarget("fp.tune.prop", "fmp_tune", "Tune"),
    makeTarget("fp.ratio.prop", "fmp_ratio", "Ratio"),
    makeTarget("fp.index.prop", "fmp_index", "Index"),
    makeTarget("fp.idec.prop", "fmp_idec", "FM Env"),
    makeTarget("fp.fdbk.prop", "fmp_fdbk", "Feedback"),
    makeTarget("fp.dive.prop", "fmp_dive", "Dive"),
    makeTarget("fp.dtime.prop", "fmp_dtime", "Dive Time"),
    makeTarget("fp.decay.prop", "fmp_decay", "Decay"),
    makeTarget("fp.fold.prop", "fmp_fold", "Fold"),
    makeTarget("fp.noise.prop", "fmp_noise", "Noise"),
};

// Compile-time guard: every row of an engine's table names a parameter under that engine's tag
// ("kick_..."), so a rename that misses a literal above fails the build instead of silencing a destination.
template <std::size_t N>
constexpr bool allUnderPrefix(const ModDestEntry (&table)[N], const char* prefix) noexcept
{
    for (const auto& row : table)
        for (std::size_t c = 0; prefix[c] != '\0'; ++c)
            if (row.param[c] != prefix[c]) return false;   // a shorter param also stops here, at its terminator
    return true;
}
static_assert(allUnderPrefix(kKickDests,   "kick_"),   "Kick targets must use the kick_ parameters");
static_assert(allUnderPrefix(kBassDests,   "bass_"),   "Bass targets must use the bass_ parameters");
static_assert(allUnderPrefix(kHatDests,    "hat_"),    "Hat targets must use the hat_ parameters");
static_assert(allUnderPrefix(kSnareDests,  "snare_"),  "Snare targets must use the snare_ parameters");
static_assert(allUnderPrefix(kRumbleDests, "rumble_"), "Rumble targets must use the rumble_ parameters");
static_assert(allUnderPrefix(kFmPercDests, "fmp_"),    "FM Perc targets must use the fmp_ parameters");

// The destination table for an engine, plus its entry count.
inline const ModDestEntry* destsForEngine(EngineType engine, int& count) noexcept
{
    switch (engine)
    {
        case EngineType::Kick:   count = (int) std::size(kKickDests);   return kKickDests;
        case EngineType::Bass:   count = (int) std::size(kBassDests);   return kBassDests;
        case EngineType::Hat:    count = (int) std::size(kHatDests);    return kHatDests;
        case EngineType::Snare:  count = (int) std::size(kSnareDests);  return kSnareDests;
        case EngineType::Rumble: count = (int) std::size(kRumbleDests); return kRumbleDests;
        case EngineType::FmPerc: count = (int) std::size(kFmPercDests); return kFmPercDests;
    }
    count = 0;
    return nullptr;
}

// True if `id` is a valid destination for `engine`: a track keeps only the assignments of the
// engine it runs.
inline bool isValidEngineDest(EngineType engine, const std::string& id) noexcept
{
    int n = 0;
    const ModDestEntry* t = destsForEngine(engine, n);
    for (int i = 0; i < n; ++i)
        if (id == t[i].id) return true;
    return false;
}

// Build the mu-core ModDestProvider for one engine (drives the destination dropdown +
// id resolution in the shared ModulatorPanel). The editor holds one per engine and
// hands the shown track's provider to the panel on selection.
inline ModDestProvider makeModDestProvider(EngineType engine)
{
    ModDestProvider p;

    p.populate = [engine](DropdownSelect& dd, int /*driveChar*/, bool /*steppedMode*/)
    {
        // The shared builder over this engine's table (engine tables have no sections).
        int n = 0;
        const ModDestEntry* t = destsForEngine(engine, n);
        mu_mod::populateDropdown(dd, t, n);
    };

    int count = 0;
    destsForEngine(engine, count);
    wireTableModDestResolve(p,
        [engine](int i) { int n = 0; return std::string(destsForEngine(engine, n)[i].id); },
        count);

    return p;
}

} // namespace mu_on
