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

// One row per target: id, dropdown label, section, per-voice parameter (without the "a{N}_"
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
    { "osc1.pos",   "Osc1 Position", "Osc", "o1_pos" },
    { "osc2.pos",   "Osc2 Position", "Osc", "o2_pos" },
    { "noise.level","Noise",      "Osc", "noise" },
    // ── Cross-mod (mu-Tant's ids) ─────────────────────────────────────────────
    { "xmod.index", "X-Mod Index", "X-Mod", "xmod_index" },
    { "xmod.depth", "X-Mod Depth", "X-Mod", "xmod_depth" },
    { "xmod.ssb",   "X-Mod SSB",   "X-Mod", "xmod_ssb" },
    // ── Filter ────────────────────────────────────────────────────────────────
    { "flt.cutoff", "Cutoff",     "Filter", "cut" },
    { "flt.res",    "Resonance",  "Filter", "res" },
    { "flt.drive",  "Drive",      "Filter", "drv" },
    { "flt.env",    "Env Depth",  "Filter", "feDep" },
    // ── Envelopes ─────────────────────────────────────────────────────────────
    { "amp.level",  "Amp Level",  "Amp", "aeL" },
    { "pitch.env",  "Pitch Env",  "Pitch", "peDep" },
    { "arp.accent", "Accent",     "Arp", "acc" },
    // ── Envelope times and levels (as mu-Clid offers its envelopes; appended, so saved assignments stay valid) ──
    { "amp.attack",   "Amp Attack",     "Amp",   "aeA" },
    { "amp.decay",    "Amp Decay",      "Amp",   "aeD" },
    { "amp.sustain",  "Amp Sustain",    "Amp",   "aeS" },
    { "amp.release",  "Amp Release",    "Amp",   "aeR" },
    { "fenv.attack",  "Filter Attack",  "Filter", "feA" },
    { "fenv.decay",   "Filter Decay",   "Filter", "feD" },
    { "fenv.sustain", "Filter Sustain", "Filter", "feS" },
    { "fenv.release", "Filter Release", "Filter", "feR" },
    { "penv.attack",  "Pitch Attack",   "Pitch", "peA" },
    { "penv.decay",   "Pitch Decay",    "Pitch", "peD" },
    { "penv.sustain", "Pitch Sustain",  "Pitch", "peS" },
    { "penv.release", "Pitch Release",  "Pitch", "peR" },
    // ── Insert (normalised 0..1, as mu-Clid and mu-Tant offer them; appended) ──
    { "insert.p1",    "Insert P1",      "Insert", "insP1" },
    { "insert.p2",    "Insert P2",      "Insert", "insP2" },
    { "insert.p3",    "Insert P3",      "Insert", "insP3" },
    { "insert.p4",    "Insert P4",      "Insert", "insP4" },
    // ── Mixer strip: pan and the FX sends (the strip's ch{N}_ parameters; same ids as mu-Clid; appended) ──
    { "amp.pan",      "Pan",            "Amp", "pan" },
    { "send.effect",  "Effect Send",    "Effects", "sendEff" },
    { "send.delay",   "Delay Send",     "Effects", "sendDly" },
    { "send.reverb",  "Reverb Send",    "Effects", "sendRev" },
};

// The out[] indices — MUST match kModDestTable order.
enum ModDestIndex
{
    D_dir, D_octs, D_inv, D_chord, D_root, D_roct, D_rate, D_gate, D_porta,
    D_o1lvl, D_o2lvl, D_o2semi, D_o1pos, D_o2pos, D_noise,
    D_xmIdx, D_xmDep, D_xmSsb,
    D_cut, D_res, D_drv, D_fenv,
    D_amp, D_penv,
    D_acc,
    D_aeA, D_aeD, D_aeS, D_aeR, D_feA, D_feD, D_feS, D_feR, D_peA, D_peD, D_peS, D_peR,
    D_insP1, D_insP2, D_insP3, D_insP4,
    D_pan, D_sendEff, D_sendDly, D_sendRev,
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

    p.populate = [](DropdownSelect& dd, int driveChar, bool /*steppedMode*/)
    {
        mu_mod::populateDropdown(dd, kModDestTable, { driveChar });   // the shared builder; driveChar = active insert effect
    };

    wireTableModDestResolve(p,
        [](int i) { return std::string(kModDestTable[i].id); },
        kNumModDests);

    return p;
}

} // namespace mu_toni
