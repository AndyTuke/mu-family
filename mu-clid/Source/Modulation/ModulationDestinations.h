#pragma once

#include <cstddef>
#include <cstring>
#include <string>
#include "Modulation/ModTarget.h"   // mu-core: the standard target row

// canonical modulation source / destination registry, lifted out of
// UI/ModulatorEditor.h so non-UI code (specifically the preset deserialiser
// in PluginProcessor_Preset.cpp) can validate `<Asgn>` entries against the
// same single source of truth that drives the destination dropdown.
//
// Before this header, `deserialiseModulators` added every assignment to the
// matrix without checking whether `sourceId` / `destinationId` resolved to
// anything the audio engine could match — a renamed parameter would silently
// turn into a no-op modulation, and the user would just see "my LFO doesn't
// work anymore" with no diagnostic in the load-error path.
//
// Each entry has:
//   - id:    the stable string ID used in saved assignments (e.g. "filter.cutoff")
//   - alias: the human-friendly label shown in the destination dropdown
//
// New destinations MUST be appended to the end. The 1-based dropdown indices
// are persisted across UI sessions, so reordering would scramble them — but
// saved .muRhythm / .muClid assignments reference the string `id`, so they
// survive reordering of the table itself.

namespace ModDest
{
    // The family-standard target row (mu-core Modulation/ModTarget.h): id, dropdown label,
    // section, per-rhythm parameter (without the "r{N}_" prefix; nullptr = reserved slot).
    // Depth is a percentage of that parameter's range.
    using Dest = mu_mod::ModTarget;

    inline const Dest kTable[] = {
        // ── Amp (idx 0–3) ─────────────────────────────────────────────────────
        { "amp.attack",       "Amp Attack", "Amp", "aEnvAtk" },
        { "amp.decay",        "Amp Decay", "Amp", "aEnvDec" },
        { "amp.sustain",      "Amp Sustain", "Amp", "aEnvSus" },
        // idx 3 ("amp.release") retired: a one-shot step trigger never note-offs
        // during playback, so the amp env never reaches its release phase and
        // modulating the release time is a no-op. Slot kept (invalid id) so kTable
        // indices don't shift; legacy "amp.release" assignments are rejected by
        // isValidDestinationId() and dropped at load. The release knob itself still
        // works (it shapes a retired voice's hot-swap tail-out).
        { "_reserved.amp.release", "(reserved)", nullptr, nullptr },
        // ── Filter (idx 4–8) ──────────────────────────────────────────────────
        { "filter.cutoff",    "Filter Cutoff", "Filter", "fltCut" },
        { "filter.resonance", "Filter Resonance", "Filter", "fltRes" },
        { "fenv.attack",      "Filter Env Attack", "Filter", "fEnvAtk" },
        { "fenv.decay",       "Filter Env Decay", "Filter", "fEnvDec" },
        { "fenv.depth",       "Filter Env Depth", "Filter", "fEnvDep" },
        // ── Pitch (idx 9) ─────────────────────────────────────────────────────
        { "pitch.semitones",  "Pitch Semitones", "Pitch", "pitchSemi" },
        // ── Insert (idx 10–15) ────────────────────────────────────────────────
        // Stage 36: 4 generic Param slots (normalised 0..1) replace the prior
        // 6 semantically-named insert destinations. The same destination ID
        // (`insert.p1`) means "knob 1 of the active insert algorithm" —
        // semantics shift with the algorithm but the modulation routing is
        // stable. Old IDs (insert.drive / .bits / .rate / .dither / .lpf /
        // .output) become invalid; the preset loader maps them per-algo to
        // the new slots on v2 → v3 migration.
        { "insert.p1",        "Insert P1", "Insert", "insP1" },
        { "insert.p2",        "Insert P2", "Insert", "insP2" },
        { "insert.p3",        "Insert P3", "Insert", "insP3" },
        { "insert.p4",        "Insert P4", "Insert", "insP4" },
        // Two reserved slots to keep downstream kTable indices stable across
        // the v2 → v3 migration (UI dropdown IDs are 1-based table indices,
        // and existing test indices reference this layout). Picked invalid
        // names so legacy preset modulation assignments to insert.dither etc.
        // are rejected by isValidDestinationId() (caller then drops them).
        { "_reserved.insert.5", "(reserved)", nullptr, nullptr },
        { "_reserved.insert.6", "(reserved)", nullptr, nullptr },
        // ── Euclid A/B pattern (idx 16–19) ────────────────────────────────────
        { "euclid.a.hits",    "Euclid A Hits", "Euclid A", "hitsA" },
        { "euclid.a.rotate",  "Euclid A Rotate", "Euclid A", "rotA" },
        { "euclid.b.hits",    "Euclid B Hits", "Euclid B", "hitsB" },
        { "euclid.b.rotate",  "Euclid B Rotate", "Euclid B", "rotB" },
        // ── Pitch octave (idx 20) ─────────────────────────────────────────────
        // idx 21 ("pitch.fine") was retired — the slot stays so subsequent
        // kTable indices don't shift (UI dropdown IDs are 1-based table indices).
        // The id below intentionally doesn't match any valid destination string,
        // so `isValidDestinationId("pitch.fine")` returns false and legacy preset
        // assignments are rejected at load.
        { "pitch.octave",     "Pitch Octave", "Pitch", "pitchOct" },
        { "_reserved.pitch.fine", "(reserved)", nullptr, nullptr },   // formerly "pitch.fine"
        // ── Euclid C pattern (idx 22–23) ──────────────────────────────────────
        { "euclid.c.hits",    "Euclid C Hits", "Euclid C", "hitsC" },
        { "euclid.c.rotate",  "Euclid C Rotate", "Euclid C", "rotC" },
        // ── Added destinations (idx 24–26) ────────────────────────────────────────
        { "pitch.envDepth",   "Pitch Env Depth", "Pitch", "pEnvDep" },
        { "amp.level",        "Amp Level", "Amp", "ampLvl" },
        { "accentDb",         "Accent", "Amp", "accentDb" },
        // ── Euclid A pad knobs (idx 27–30) ────────────────────────────────────
        { "euclid.a.prePad",  "Euclid A Pre Pad", "Euclid A", "prePadA" },
        { "euclid.a.postPad", "Euclid A Post Pad", "Euclid A", "postPadA" },
        { "euclid.a.insSt",   "Euclid A Insert Start", "Euclid A", "insStA" },
        { "euclid.a.insLen",  "Euclid A Insert Length", "Euclid A", "insLenA" },
        // ── Euclid B pad knobs (idx 31–34) ────────────────────────────────────
        { "euclid.b.prePad",  "Euclid B Pre Pad", "Euclid B", "prePadB" },
        { "euclid.b.postPad", "Euclid B Post Pad", "Euclid B", "postPadB" },
        { "euclid.b.insSt",   "Euclid B Insert Start", "Euclid B", "insStB" },
        { "euclid.b.insLen",  "Euclid B Insert Length", "Euclid B", "insLenB" },
        // ── Euclid C pad knobs (idx 35–38) ────────────────────────────────────
        { "euclid.c.prePad",  "Euclid C Pre Pad", "Euclid C", "prePadC" },
        { "euclid.c.postPad", "Euclid C Post Pad", "Euclid C", "postPadC" },
        { "euclid.c.insSt",   "Euclid C Insert Start", "Euclid C", "insStC" },
        { "euclid.c.insLen",  "Euclid C Insert Length", "Euclid C", "insLenC" },
        // ── (Reserved) Stage 36 retired ks.* / voc.* destinations ─────────────
        // Karplus and Vocoder now share the generic insert.p1..p4 destinations
        // since each algorithm's slots have their own clean per-algo range
        // (no more "Karplus Octave packed into insertBits" overload). Legacy
        // assignments are mapped to the new slots in v2 → v3 preset migration.
        { "_reserved.ks.note",   "(reserved)", nullptr, nullptr },
        { "_reserved.ks.octave", "(reserved)", nullptr, nullptr },
        { "_reserved.voc.note",  "(reserved)", nullptr, nullptr },
        { "_reserved.voc.octave","(reserved)", nullptr, nullptr },
        { "_reserved.voc.unison","(reserved)", nullptr, nullptr },
        // ── T5 follow-up (idx 44+) ─────────────────────────────────────────────
        { "filter.lowCut",       "Filter Low Cut", "Filter", "fltLoCut" },
        // ── Mixer strip (idx 45–48): pan + FX sends — the mixer channel's ch{N}_ params ─
        { "amp.pan",          "Pan", "Amp", "pan" },
        { "send.effect",      "Effect Send", "Effects", "sendEff" },
        { "send.delay",       "Delay Send", "Effects", "sendDly" },
        { "send.reverb",      "Reverb Send", "Effects", "sendRev" },
    };
    static constexpr int kTableSize = (int)(sizeof(kTable) / sizeof(kTable[0]));

    // True for a target that drives the mixer channel (ch{N}_ pan / sends) rather than a
    // per-rhythm r{N}_ parameter.
    inline bool isMixerStripParam(const char* param) noexcept
    {
        for (const char* s : { "pan", "sendEff", "sendDly", "sendRev" })
            if (std::strcmp(param, s) == 0) return true;
        return false;
    }

    // validate that `destId` resolves to a known destination. Returns the
    // dropdown alias (for diagnostic messages) when found, nullptr otherwise.
    inline const char* aliasFor(const std::string& destId) noexcept
    {
        for (const auto& d : kTable)
            if (destId == d.id) return d.label;
        return nullptr;
    }

    inline bool isValidDestinationId(const std::string& destId) noexcept
    {
        return aliasFor(destId) != nullptr;
    }

    // source-ID validator. Sources come in two shapes:
    //   "csN_output" for N in 0..7  — a ControlSequence output
    //   "assign_{id}_depth"          — another assignment's depth (meta-modulation)
    // The {id} is dynamic per assignment so we can only validate the prefix/suffix.
    inline bool isValidSourceId(const std::string& srcId) noexcept
    {
        // "csN_output" — length 10, "cs" + one digit + "_output"
        if (srcId.size() == 10
            && srcId[0] == 'c' && srcId[1] == 's'
            && srcId[2] >= '0' && srcId[2] <= '9'
            && srcId.compare(3, 7, "_output") == 0)
            return true;

        // "assign_{id}_depth"
        static constexpr const char* kMetaPrefix = "assign_";
        static constexpr const char* kMetaSuffix = "_depth";
        const std::size_t pn = std::strlen(kMetaPrefix);
        const std::size_t sn = std::strlen(kMetaSuffix);
        if (srcId.size() > pn + sn
            && srcId.compare(0, pn, kMetaPrefix) == 0
            && srcId.compare(srcId.size() - sn, sn, kMetaSuffix) == 0)
            return true;

        return false;
    }
}
