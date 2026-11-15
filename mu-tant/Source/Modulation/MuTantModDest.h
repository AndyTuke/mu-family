#pragma once

#include "UI/ModulatorEditor.h"           // mu-core: ModDestProvider
#include "UI/Components/DropdownSelect.h"
#include "Modulation/ModTarget.h"         // mu-core: the standard target row
#include "UI/TableModDestProvider.h"      // mu-core: the table-driven destination provider

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
    { "osc1.octave",  "Osc1 Octave",   "Osc 1", "o1_oct", 3, true },
    { "osc1.semi",    "Osc1 Semi",     "Osc 1", "o1_semi", 12 },
    { "osc1.fine",    "Osc1 Fine",     "Osc 1", "o1_fine" },
    { "osc1.pos",     "Osc1 Position", "Osc 1", "o1_pos" },
    { "osc1.penv.prop", "Pitch Env",   "Osc 1", "o1_penv_depth" },
    // ── Pitch (osc2) ────────────────────────────────────────────────────────
    { "osc2.octave",  "Osc2 Octave",   "Osc 2", "o2_oct", 3, true },
    { "osc2.semi",    "Osc2 Semi",     "Osc 2", "o2_semi", 12 },
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
    // ── Mixer strip: pan and the FX sends (the strip's ch{N}_ parameters; same ids as mu-Clid) ──
    { "amp.pan",      "Pan",           "Amp", "pan" },
    { "send.effect",  "Effect Send",   "Effects", "sendEff" },
    { "send.delay",   "Delay Send",    "Effects", "sendDly" },
    { "send.reverb",  "Reverb Send",   "Effects", "sendRev" },
};

inline constexpr int kModDestCount = (int) (sizeof(kModDestTable) / sizeof(kModDestTable[0]));

// The provider is the shared table one: stepped-snap units and the stepped-only octave rule are fields on the rows above.
inline ModDestProvider makeModDestProvider()
{
    return mu_mod::makeTableDestProvider(kModDestTable, kModDestCount);
}

} // namespace mu_tant
