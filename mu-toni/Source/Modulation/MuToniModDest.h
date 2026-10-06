#pragma once

#include "UI/ModulatorEditor.h"           // mu-core: ModDestProvider + wireTableModDestResolve
#include "UI/Components/DropdownSelect.h"
#include "Modulation/ModTarget.h"         // mu-core: the standard target row
#include "UI/ModTargetDropdown.h"        // mu-core: the shared destination-dropdown builder

#include <array>
#include <string>
#include <mutex>

// μ-Toni's modulation-destination registry + provider. Mirrors mu-tant's
// MuTantModDest.h: each destination maps a stable string `id` (used in saved
// assignments + the audio-thread paramValues map) to a dropdown alias + section.
// All dests are resolved through the shared mu_mod::resolveLane in PROPORTION
// space: depth is a percentage of the parameter's range (family standard).
// Order MUST match the D_* enum below + the modDestRanges built in the processor.
namespace mu_toni
{

// One row per target: id, dropdown label, section, per-voice parameter (without the "v{N}_"
// prefix). The family-standard row — see mu-core Modulation/ModTarget.h.
using ModDest = mu_mod::ModTarget;

inline constexpr ModDest kModDestTable[] = {
    // ── Arpeggiator ───────────────────────────────────────────────────────────
    { "arp.dir",    "Direction",  "Arp", "dir" },
    { "arp.octs",   "Octaves",    "Arp", "octs" },
    { "arp.inv",    "Inversion",  "Arp", "inv" },
    { "arp.chord",  "Chord",      "Arp", "chord" },
    { "arp.root",   "Root",       "Arp", "root" },
    { "arp.roct",   "Root Octave","Arp", "roct" },
    { "arp.rate",   "Rate",       "Arp", "rate" },
    { "arp.gate",   "Gate Length","Arp", "gate" },
    { "arp.porta",  "Portamento", "Arp", "porta" },
    // ── Oscillators ───────────────────────────────────────────────────────────
    { "osc1.level", "Osc1 Level", "Osc", "o1l" },
    { "osc2.level", "Osc2 Level", "Osc", "o2l" },
    { "osc2.semi",  "Osc2 Semi",  "Osc", "o2s" },
    { "osc.pw",     "Pulse Width","Osc", "pw" },
    { "noise.level","Noise",      "Osc", "noise" },
    // ── Filter ────────────────────────────────────────────────────────────────
    { "flt.cutoff", "Cutoff",     "Filter", "cut" },
    { "flt.res",    "Resonance",  "Filter", "res" },
    { "flt.drive",  "Drive",      "Filter", "drv" },
    { "flt.env",    "Env Depth",  "Filter", "feDep" },
    // ── Envelopes ─────────────────────────────────────────────────────────────
    { "amp.level",  "Amp Level",  "Amp", "aeL" },
    { "pitch.env",  "Pitch Env",  "Pitch", "peDep" },
};

// The out[] indices — MUST match kModDestTable order.
enum ModDestIndex
{
    D_dir, D_octs, D_inv, D_chord, D_root, D_roct, D_rate, D_gate, D_porta,
    D_o1lvl, D_o2lvl, D_o2semi, D_pw, D_noise,
    D_cut, D_res, D_drv, D_fenv,
    D_amp, D_penv,
    kNumModDests
};

static_assert((int) (sizeof(kModDestTable) / sizeof(kModDestTable[0])) == kNumModDests,
              "kModDestTable size must equal kNumModDests");

inline bool isValidModDest(const std::string& id)
{
    for (const auto& d : kModDestTable) if (id == d.id) return true;
    return false;
}

inline ModDestProvider makeModDestProvider()
{
    ModDestProvider p;
    p.unitsPerDirection = [](const std::string&) { return 0; };   // no stepped-snap graphic for MVP

    p.populate = [](DropdownSelect& dd, int /*driveChar*/, bool /*steppedMode*/)
    {
        mu_mod::populateDropdown(dd, kModDestTable);   // the shared builder
    };

    wireTableModDestResolve(p,
        [](int i) { return std::string(kModDestTable[i].id); },
        kNumModDests);

    return p;
}

} // namespace mu_toni
