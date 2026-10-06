#pragma once

#include "UI/ModulatorEditor.h"           // mu-core: ModDestProvider
#include "UI/Components/DropdownSelect.h"
#include "Modulation/ModTarget.h"         // mu-core: the standard target row
#include "UI/ModTargetDropdown.h"        // mu-core: the shared destination-dropdown builder

#include <array>
#include <cstring>
#include <mutex>
#include <string_view>

// mu-tant's modulation-destination registry + provider.
//
// Each entry has:
//   - id:    the stable string ID used in saved assignments + the audio-thread
//            paramValues map (e.g. "filter.cutoff", "osc1.pos").
//   - label: the human-friendly label shown in the destination dropdown.
//   - section / param: dropdown heading and the per-voice parameter it drives.
// Depth is a percentage of that parameter's range (family standard, ModTarget.h).
//
// New destinations MUST be appended to the end. The 1-based dropdown indices
// are persisted across UI sessions; saved .muTant assignments reference the
// string `id`, so reordering this table only scrambles dropdown IDs of
// already-open editors — saved presets survive.
namespace mu_tant
{

// One row per target: id, dropdown label, section, per-voice parameter (without the "v{N}_"
// prefix). The family-standard row — see mu-core Modulation/ModTarget.h.
using ModDest = mu_mod::ModTarget;

inline constexpr ModDest kModDestTable[] = {
    // ── Pitch (osc1) ────────────────────────────────────────────────────────
    { "osc1.octave",  "Osc1 Octave",   "Osc 1", "o1_oct" },
    { "osc1.semi",    "Osc1 Semi",     "Osc 1", "o1_semi" },
    { "osc1.fine",    "Osc1 Fine",     "Osc 1", "o1_fine" },
    { "osc1.pos",     "Osc1 Position", "Osc 1", "o1_pos" },
    { "osc1.penv.prop", "Pitch Env",   "Osc 1", "o1_penv_depth" },
    // ── Pitch (osc2) ────────────────────────────────────────────────────────
    { "osc2.octave",  "Osc2 Octave",   "Osc 2", "o2_oct" },
    { "osc2.semi",    "Osc2 Semi",     "Osc 2", "o2_semi" },
    { "osc2.fine",    "Osc2 Fine",     "Osc 2", "o2_fine" },
    { "osc2.pos",     "Osc2 Position", "Osc 2", "o2_pos" },
    { "osc2.penv.prop", "Pitch Env",   "Osc 2", "o2_penv_depth" },
    // ── Cross-mod (2-lane bus model — mu-tant-xmod-design.md) ─────────────────
    { "xmod.index",   "X-Mod Index",   "X-Mod", "xmod_index" },
    { "xmod.depth",   "X-Mod Depth",   "X-Mod", "xmod_depth" },
    { "xmod.ssb",     "X-Mod SSB",     "X-Mod", "xmod_ssb" },
    // ── Levels ────────────────────────────────────────────────────────────────
    { "osc1.level",   "Osc1 Level",    "Levels", "o1_lvl" },
    { "osc2.level",   "Osc2 Level",    "Levels", "o2_lvl" },
    { "noise.level",  "Noise Level",   "Levels", "noise_lvl" },
    // ── Filter 1 (cutoff/resonance are shared mu-core dests; drive/lo-cut use the
    //    ".prop" proportion convention → depthScaleFor=1.0, no mu-core edit) ──────
    { "filter.cutoff",     "Cutoff",     "Filter 1", "flt_cut" },
    { "filter.resonance",  "Resonance",  "Filter 1", "flt_res" },
    { "filter.drive.prop", "Drive",      "Filter 1", "flt_drv" },
    { "filter.locut.prop", "Low Cut",    "Filter 1", "flt_lo_cut" },
    { "filter.env.prop",   "Env Depth",  "Filter 1", "flt_env_depth" },
    // ── Filter 2 (proportion-space — ".prop" → depthScaleFor=1.0, no mu-core edit) ──
    { "filter2.cutoff.prop",    "Cutoff",     "Filter 2", "flt2_cut" },
    { "filter2.resonance.prop", "Resonance",  "Filter 2", "flt2_res" },
    { "filter2.drive.prop",     "Drive",      "Filter 2", "flt2_drv" },
    { "filter2.locut.prop",     "Low Cut",    "Filter 2", "flt2_lo_cut" },
    { "filter2.env.prop",       "Env Depth",  "Filter 2", "flt2_env_depth" },
    // ── Amp ─────────────────────────────────────────────────────────────────
    { "level",        "Level",         "Amp", "level" },
    // ── Insert (normalised 0..1 — same IDs as mu-clid so depthScaleFor=1.0) ──
    { "insert.p1",    "Insert P1",     "Insert", "insP1" },
    { "insert.p2",    "Insert P2",     "Insert", "insP2" },
    { "insert.p3",    "Insert P3",     "Insert", "insP3" },
    { "insert.p4",    "Insert P4",     "Insert", "insP4" },
};

inline constexpr int kModDestCount = (int) (sizeof(kModDestTable) / sizeof(kModDestTable[0]));

// Discrete units per direction for a destination, so a STEPPED modulator's editor snaps
// the graphic to whole units (an octave = 3 steps up/down, a scale degree = 12). Continuous
// destinations (cutoff, levels, position, fine, x-mod…) return 0 → no snapping.
inline int unitsPerDirectionFor(const std::string& destId)
{
    if (destId == "osc1.octave" || destId == "osc2.octave") return 3;    // ±3 octaves
    if (destId == "osc1.semi"   || destId == "osc2.semi")   return 12;   // ±12 scale degrees
    return 0;
}

// Destinations that only make sense as discrete jumps — a smooth (LFO-style) sweep of them
// isn't musical, so they're hidden from the destination list when the modulator is Smooth.
// Octave is the canonical case (a continuous octave glide); scale-degree stays available
// (it glides between degrees for the envelope / smooth modulators).
inline bool isSteppedOnlyDest(const char* destId)
{
    return std::strcmp(destId, "osc1.octave") == 0 || std::strcmp(destId, "osc2.octave") == 0;
}

inline ModDestProvider makeModDestProvider()
{
    ModDestProvider p;
    p.unitsPerDirection = [](const std::string& destId) { return unitsPerDirectionFor(destId); };

    p.populate = [](DropdownSelect& dd, int driveChar, bool steppedMode)
    {
        // The shared builder; in Smooth mode stepped-only targets (octave) are omitted.
        mu_mod::populateDropdown(dd, kModDestTable,
            { driveChar, [steppedMode](const mu_mod::ModTarget& t) { return steppedMode || ! isSteppedOnlyDest(t.id); } });
    };

    wireTableModDestResolve(p,
        [](int i) { return std::string(kModDestTable[i].id); },
        kModDestCount);

    return p;
}

} // namespace mu_tant
