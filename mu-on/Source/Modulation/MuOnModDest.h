#pragma once

#include "UI/ModulatorEditor.h"            // mu-core: ModDestProvider
#include "UI/Components/DropdownSelect.h"   // mu-core
#include "Plugin/MuOnChannels.h"           // Channel enum
#include "Modulation/ModTarget.h"         // mu-core: the standard target row
#include "UI/ModTargetDropdown.h"        // mu-core: the shared destination-dropdown builder

#include <cstddef>
#include <cstring>
#include <string>

// μ-On modulation-destination registry — one table per FIXED instrument lane
// (Kick / Bass / Hat / Snare). Unlike mu-tant (every voice identical), μ-On's lanes
// expose different engine params, so each lane has its own destination set and the
// editor swaps the ModulatorPanel's destination provider when the lane changes.
//
// Each entry carries the stable id saved in assignments (historically ending ".prop"), the
// APVTS parameter it drives and its dropdown label. Like every family target, depth is a
// percentage of the parameter's range: the engine seeds the slider's 0..1 proportion, the
// matrix offsets it, and resolveLane converts back via the param's NormalisableRange.
//
// The table ORDER is the single source of truth: it equals the order GrooveVoices feeds
// the modulated values back into each engine's setParams(), so reordering a lane's table
// without matching the engine dispatch would scramble the mapping. New dests append.
namespace mu_on
{

// One row per target — the family-standard row (mu-core Modulation/ModTarget.h): id, dropdown
// label, section (none: each lane is its own list), parameter. Rows below are written
// { id, param, label } for readability, so they go through makeTarget.
using ModDestEntry = mu_mod::ModTarget;
constexpr ModDestEntry makeTarget(const char* id, const char* param, const char* label) { return { id, label, nullptr, param }; }

// Kick — order matches KickEngine::setParams(baseHz, pitchAmtHz, pitchDecMs, ampDecMs, drv).
inline constexpr ModDestEntry kKickDests[] = {
    makeTarget("k.tune.prop", "k_tune", "Tune"),
    makeTarget("k.ptch.prop", "k_ptch", "Pitch Amt"),
    makeTarget("k.pdec.prop", "k_pdec", "Pitch Decay"),
    makeTarget("k.adec.prop", "k_adec", "Decay"),
    makeTarget("k.drive.prop", "k_drive", "Drive"),
};

// Bass — order matches BassEngine::setParams(rootHz, [wave], sub, cutHz, res, env,
// edecMs, atkMs, decMs, sus, drv). Wave is a choice param — not modulatable, omitted.
inline constexpr ModDestEntry kBassDests[] = {
    makeTarget("b.tune.prop", "b_tune", "Tune"),
    makeTarget("b.sub.prop", "b_sub", "Sub"),
    makeTarget("b.cut.prop", "b_cut", "Cutoff"),
    makeTarget("b.res.prop", "b_res", "Resonance"),
    makeTarget("b.env.prop", "b_env", "Filter Env"),
    makeTarget("b.edec.prop", "b_edec", "Env Decay"),
    makeTarget("b.atk.prop", "b_atk", "Attack"),
    makeTarget("b.dec.prop", "b_dec", "Decay"),
    makeTarget("b.sus.prop", "b_sus", "Sustain"),
    makeTarget("b.drive.prop", "b_drive", "Drive"),
};

// Hat — order matches SampleChannel::setParams(tuneSemitones, decayMs).
inline constexpr ModDestEntry kHatDests[] = {
    makeTarget("h.tune.prop", "h_tune", "Tune"),
    makeTarget("h.dec.prop", "h_dec", "Decay"),
};

// Snare — same SampleChannel::setParams order.
inline constexpr ModDestEntry kSnareDests[] = {
    makeTarget("s.tune.prop", "s_tune", "Tune"),
    makeTarget("s.dec.prop", "s_dec", "Decay"),
};

// Rumble — order matches RumbleEngine::setParams(bpm, drive, d1, d2, d3, revSize, revMix, revLpHz, cutHz, res)
// (bpm is the transport, not a dest — the 9 entries below are the modulatable args in order).
inline constexpr ModDestEntry kRumbleDests[] = {
    makeTarget("r.drive.prop", "r_drive", "Drive"),
    makeTarget("r.d1.prop", "r_d1", "1/16"),
    makeTarget("r.d2.prop", "r_d2", "2/16"),
    makeTarget("r.d3.prop", "r_d3", "3/16"),
    makeTarget("r.size.prop", "r_size", "Rev Size"),
    makeTarget("r.revmix.prop", "r_revmix", "Rev Mix"),
    makeTarget("r.revlp.prop", "r_revlp", "Rev LP"),
    makeTarget("r.cut.prop", "r_cut", "Cutoff"),
    makeTarget("r.res.prop", "r_res", "Resonance"),
};

// The destination table for a lane (Channel enum), plus its entry count.
inline const ModDestEntry* destsForLane(int lane, int& count) noexcept
{
    switch (lane)
    {
        case Kick:  count = (int) std::size(kKickDests);  return kKickDests;
        case Bass:  count = (int) std::size(kBassDests);  return kBassDests;
        case Hat:    count = (int) std::size(kHatDests);    return kHatDests;
        case Snare:  count = (int) std::size(kSnareDests);  return kSnareDests;
        case Rumble: count = (int) std::size(kRumbleDests); return kRumbleDests;
        default:     count = 0;                             return nullptr;
    }
}

// True if `id` is a valid destination for `lane` — used to drop foreign assignments
// on preset load (each Layer belongs to one lane).
inline bool isValidLaneDest(int lane, const std::string& id) noexcept
{
    int n = 0;
    const ModDestEntry* t = destsForLane(lane, n);
    for (int i = 0; i < n; ++i)
        if (id == t[i].id) return true;
    return false;
}

// Build the mu-core ModDestProvider for one lane (drives the destination dropdown +
// id resolution in the shared ModulatorPanel). The editor holds one per lane and
// hands the active lane's provider to the panel on selection.
inline ModDestProvider makeModDestProvider(int lane)
{
    ModDestProvider p;

    p.populate = [lane](DropdownSelect& dd, int /*driveChar*/, bool /*steppedMode*/)
    {
        // The shared builder over this lane's table (lane tables have no sections).
        int n = 0;
        const ModDestEntry* t = destsForLane(lane, n);
        mu_mod::populateDropdown(dd, t, n);
    };

    int count = 0;
    destsForLane(lane, count);
    wireTableModDestResolve(p,
        [lane](int i) { int n = 0; return std::string(destsForLane(lane, n)[i].id); },
        count);

    return p;
}

} // namespace mu_on
