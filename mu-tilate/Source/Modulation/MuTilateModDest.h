#pragma once

#include "Modulation/ModTarget.h"   // mu-core: the standard target row

#include <string>

// mu-Tilate's modulation targets: one table, one row per target (id, dropdown label, section, parameter without the
// p{N}_ prefix). Depth is a percentage of the target knob's range (family standard). New targets are appended.
// The mixer-strip targets (pan and the FX sends) join the table with the layer, as in the other products.
namespace mu_tilate
{

using ModDest = mu_mod::ModTarget;

inline constexpr ModDest kModDestTable[] = {
    { "play.level",    "Level",          "Playback", "gain" },
    { "stretch.hold",  "Transient Hold", "Stretch",  "hold" },
    { "stretch.str",   "Strength",       "Stretch",  "strength" },
    { "slice.fadein",  "Fade In",        "Slice",    "fadeIn" },
    { "slice.fadeout", "Fade Out",       "Slice",    "fadeOut" },
    { "insert.p1",     "Insert P1",      "Insert",   "insP1" },
    { "insert.p2",     "Insert P2",      "Insert",   "insP2" },
    { "insert.p3",     "Insert P3",      "Insert",   "insP3" },
    { "insert.p4",     "Insert P4",      "Insert",   "insP4" },
    // The rest of the layer's continuous parameters. Choice rows (tempo mode, grid, window, filter type, insert algorithm) and the
    // detector's settings are not targets: they are not read per block.
    { "play.tempo",    "Loop Tempo",     "Playback", "ltempo" },
    { "stretch.mix",   "Pitch / Stretch", "Stretch", "tmix" },
    { "pitch.semi",    "Pitch",          "Pitch",    "pSemi" },
    { "pitch.fine",    "Fine",           "Pitch",    "pFine" },
    { "play.pan",      "Pan",            "Playback", "pPan" },
    { "filter.cut",    "Cutoff",         "Filter",   "fCut" },
    { "filter.res",    "Resonance",      "Filter",   "fRes" },
    { "filter.env",    "Envelope Depth", "Filter",   "fEnv" },
    { "filter.attack", "Filter Attack",  "Filter",   "fltA" },
    { "filter.decay",  "Filter Decay",   "Filter",   "fltD" },
    { "filter.sus",    "Filter Sustain", "Filter",   "fltS" },
    { "filter.rel",    "Filter Release", "Filter",   "fltR" },
    { "amp.attack",    "Amp Attack",     "Amp",      "ampA" },
    { "amp.decay",     "Amp Decay",      "Amp",      "ampD" },
    { "amp.sustain",   "Amp Sustain",    "Amp",      "ampS" },
    { "amp.release",   "Amp Release",    "Amp",      "ampR" },
    // The mixer strip: pan and the FX sends (the strip's ch{N}_ parameters; same ids as mu-Clid and mu-Toni; appended).
    { "amp.pan",       "Mixer Pan",      "Mixer",    "pan" },
    { "send.effect",   "Effect Send",    "Effects",  "sendEff" },
    { "send.delay",    "Delay Send",     "Effects",  "sendDly" },
    { "send.reverb",   "Reverb Send",    "Effects",  "sendRev" },
};

// The out[] indices: MUST match kModDestTable order.
enum ModDestIndex
{
    D_level, D_hold, D_strength, D_fadeIn, D_fadeOut, D_insP1, D_insP2, D_insP3, D_insP4,
    D_loopTempo, D_tempoMix, D_pitch, D_fine, D_pan, D_cutoff, D_res, D_filterEnv, D_fltA, D_fltD, D_fltS, D_fltR,
    D_ampA, D_ampD, D_ampS, D_ampR,
    D_stripPan, D_sendEff, D_sendDly, D_sendRev,
    kNumModDests
};

static_assert((int) (sizeof(kModDestTable) / sizeof(kModDestTable[0])) == kNumModDests,
              "kModDestTable size must equal kNumModDests");

inline bool isValidModDest(const std::string& id) { return mu_mod::isValidTarget(kModDestTable, id); }

} // namespace mu_tilate
