#pragma once

#include "UI/ModulatorEditor.h"            // mu-core: ModDestProvider
#include "UI/Components/DropdownSelect.h"   // mu-core
#include "Persistence/TrackParams.h"            // EngineType
#include "Modulation/ModTarget.h"         // mu-core: the standard target row
#include "Modulation/StripMods.h"     // mu-core: the sends as modulation targets
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
constexpr ModDestEntry makeTarget(const char* id, const char* param, const char* label, const char* section = nullptr) { return { id, label, section, param }; }

// Kick — order matches KickEngine::setParams(baseHz, pitchAmtHz, pitchDecMs, ampDecMs, drv).
inline constexpr ModDestEntry kKickDests[] = {
    makeTarget("k.tune.prop", "kick_tune", "Tune", "Kick"),
    makeTarget("k.ptch.prop", "kick_ptch", "Pitch Amt", "Kick"),
    makeTarget("k.pdec.prop", "kick_pdec", "Pitch Decay", "Kick"),
    makeTarget("k.adec.prop", "kick_adec", "Decay", "Kick"),
    makeTarget("k.drive.prop", "kick_drive", "Drive", "Kick"),
    // A drum or percussion table has six more rows just before those nine (Track::resolve reads them from the end): the
    // filter's Cutoff, Resonance, Env Attack, Env Decay and Env Depth, then the track's Ghost level.
    makeTarget("filter.cut", "kick_fcut", "Filter Cutoff", "Filter"),
    makeTarget("filter.res", "kick_fres", "Filter Resonance", "Filter"),
    makeTarget("filter.atk", "kick_fatk", "Filter Env Attack", "Filter"),
    makeTarget("filter.dec", "kick_fdec", "Filter Env Decay", "Filter"),
    makeTarget("filter.dep", "kick_fdep", "Filter Env Depth", "Filter"),
    makeTarget("groove.ghost", "ghost", "Ghost", "Groove"),
    // The same nine rows end every table, in this order (Track::resolve reads them from the end): the track's Level, the mixer
    // strip's Pan, the insert's four knobs and the strip's three sends. Appended last: saved destination indices stay valid.
    makeTarget("amp.level", "level", "Level", "Amp"),
    makeTarget("amp.pan", "pan", "Pan", "Amp"),
    makeTarget("insert.p1", "insP1", "Insert P1", "Insert"),
    makeTarget("insert.p2", "insP2", "Insert P2", "Insert"),
    makeTarget("insert.p3", "insP3", "Insert P3", "Insert"),
    makeTarget("insert.p4", "insP4", "Insert P4", "Insert"),
    makeTarget("send.effect", "sendEff", "Effect Send", "Effects"),
    makeTarget("send.delay", "sendDly", "Delay Send", "Effects"),
    makeTarget("send.reverb", "sendRev", "Reverb Send", "Effects"),
};

// Bass — order matches BassEngine::setParams(rootHz, [wave], sub, cutHz, res, env,
// edecMs, atkMs, decMs, sus, drv, relMs, glideMs). Wave is a choice param — not modulatable, omitted.
inline constexpr ModDestEntry kBassDests[] = {
    makeTarget("b.tune.prop", "bass_tune", "Tune", "Bass"),
    makeTarget("b.sub.prop", "bass_sub", "Sub", "Bass"),
    makeTarget("b.cut.prop", "bass_cut", "Cutoff", "Bass"),
    makeTarget("b.res.prop", "bass_res", "Resonance", "Bass"),
    makeTarget("b.env.prop", "bass_env", "Filter Env", "Bass"),
    makeTarget("b.edec.prop", "bass_edec", "Env Decay", "Bass"),
    makeTarget("b.atk.prop", "bass_atk", "Attack", "Bass"),
    makeTarget("b.dec.prop", "bass_dec", "Decay", "Bass"),
    makeTarget("b.sus.prop", "bass_sus", "Sustain", "Bass"),
    makeTarget("b.drive.prop", "bass_drive", "Drive", "Bass"),
    makeTarget("b.rel.prop", "bass_rel", "Release", "Bass"),     // appended last: saved destination indices stay valid
    makeTarget("b.glide.prop", "bass_glide", "Glide", "Bass"),
    // The same nine rows end every table, in this order (Track::resolve reads them from the end): the track's Level, the mixer
    // strip's Pan, the insert's four knobs and the strip's three sends. Appended last: saved destination indices stay valid.
    makeTarget("amp.level", "level", "Level", "Amp"),
    makeTarget("amp.pan", "pan", "Pan", "Amp"),
    makeTarget("insert.p1", "insP1", "Insert P1", "Insert"),
    makeTarget("insert.p2", "insP2", "Insert P2", "Insert"),
    makeTarget("insert.p3", "insP3", "Insert P3", "Insert"),
    makeTarget("insert.p4", "insP4", "Insert P4", "Insert"),
    makeTarget("send.effect", "sendEff", "Effect Send", "Effects"),
    makeTarget("send.delay", "sendDly", "Delay Send", "Effects"),
    makeTarget("send.reverb", "sendRev", "Reverb Send", "Effects"),
};

// Hat — order matches SampleChannel::setParams(tuneSemitones, decayMs, openTuneSemitones, openDecayMs).
inline constexpr ModDestEntry kHatDests[] = {
    makeTarget("h.tune.prop", "hat_tune", "Tune", "Hat"),
    makeTarget("h.dec.prop", "hat_dec", "Decay", "Hat"),
    makeTarget("h.otune.prop", "hat_otune", "Open Tune", "Hat"),
    makeTarget("h.odec.prop", "hat_odec", "Open Decay", "Hat"),
    // A drum or percussion table has six more rows just before those nine (Track::resolve reads them from the end): the
    // filter's Cutoff, Resonance, Env Attack, Env Decay and Env Depth, then the track's Ghost level.
    makeTarget("filter.cut", "hat_fcut", "Filter Cutoff", "Filter"),
    makeTarget("filter.res", "hat_fres", "Filter Resonance", "Filter"),
    makeTarget("filter.atk", "hat_fatk", "Filter Env Attack", "Filter"),
    makeTarget("filter.dec", "hat_fdec", "Filter Env Decay", "Filter"),
    makeTarget("filter.dep", "hat_fdep", "Filter Env Depth", "Filter"),
    makeTarget("groove.ghost", "ghost", "Ghost", "Groove"),
    // The same nine rows end every table, in this order (Track::resolve reads them from the end): the track's Level, the mixer
    // strip's Pan, the insert's four knobs and the strip's three sends. Appended last: saved destination indices stay valid.
    makeTarget("amp.level", "level", "Level", "Amp"),
    makeTarget("amp.pan", "pan", "Pan", "Amp"),
    makeTarget("insert.p1", "insP1", "Insert P1", "Insert"),
    makeTarget("insert.p2", "insP2", "Insert P2", "Insert"),
    makeTarget("insert.p3", "insP3", "Insert P3", "Insert"),
    makeTarget("insert.p4", "insP4", "Insert P4", "Insert"),
    makeTarget("send.effect", "sendEff", "Effect Send", "Effects"),
    makeTarget("send.delay", "sendDly", "Delay Send", "Effects"),
    makeTarget("send.reverb", "sendRev", "Reverb Send", "Effects"),
};

// Snare — SampleChannel::setParams(tuneSemitones, decayMs, snap).
inline constexpr ModDestEntry kSnareDests[] = {
    makeTarget("s.tune.prop", "snare_tune", "Tune", "Snare"),
    makeTarget("s.dec.prop", "snare_dec", "Decay", "Snare"),
    makeTarget("s.snap.prop", "snare_snap", "Snap", "Snare"),
    // A drum or percussion table has six more rows just before those nine (Track::resolve reads them from the end): the
    // filter's Cutoff, Resonance, Env Attack, Env Decay and Env Depth, then the track's Ghost level.
    makeTarget("filter.cut", "snare_fcut", "Filter Cutoff", "Filter"),
    makeTarget("filter.res", "snare_fres", "Filter Resonance", "Filter"),
    makeTarget("filter.atk", "snare_fatk", "Filter Env Attack", "Filter"),
    makeTarget("filter.dec", "snare_fdec", "Filter Env Decay", "Filter"),
    makeTarget("filter.dep", "snare_fdep", "Filter Env Depth", "Filter"),
    makeTarget("groove.ghost", "ghost", "Ghost", "Groove"),
    // The same nine rows end every table, in this order (Track::resolve reads them from the end): the track's Level, the mixer
    // strip's Pan, the insert's four knobs and the strip's three sends. Appended last: saved destination indices stay valid.
    makeTarget("amp.level", "level", "Level", "Amp"),
    makeTarget("amp.pan", "pan", "Pan", "Amp"),
    makeTarget("insert.p1", "insP1", "Insert P1", "Insert"),
    makeTarget("insert.p2", "insP2", "Insert P2", "Insert"),
    makeTarget("insert.p3", "insP3", "Insert P3", "Insert"),
    makeTarget("insert.p4", "insP4", "Insert P4", "Insert"),
    makeTarget("send.effect", "sendEff", "Effect Send", "Effects"),
    makeTarget("send.delay", "sendDly", "Delay Send", "Effects"),
    makeTarget("send.reverb", "sendRev", "Reverb Send", "Effects"),
};

// Rumble — order matches RumbleEngine::setParams(bpm, drive, d1, d2, d3, revSize, revMix, revLpHz, cutHz, res, inLpHz)
// (bpm is the transport, not a dest — the 10 entries below are the modulatable args in order).
inline constexpr ModDestEntry kRumbleDests[] = {
    makeTarget("r.drive.prop", "rumble_drive", "Drive", "Rumble"),
    makeTarget("r.d1.prop", "rumble_d1", "1/16", "Rumble"),
    makeTarget("r.d2.prop", "rumble_d2", "2/16", "Rumble"),
    makeTarget("r.d3.prop", "rumble_d3", "3/16", "Rumble"),
    makeTarget("r.size.prop", "rumble_size", "Rev Size", "Rumble"),
    makeTarget("r.revmix.prop", "rumble_revmix", "Rev Mix", "Rumble"),
    makeTarget("r.revlp.prop", "rumble_revlp", "Rev LP", "Rumble"),
    makeTarget("r.cut.prop", "rumble_cut", "Cutoff", "Rumble"),
    makeTarget("r.res.prop", "rumble_res", "Resonance", "Rumble"),
    makeTarget("r.inlp.prop", "rumble_inlp", "Input LP", "Rumble"),   // appended last: saved destination indices stay valid
    // The same nine rows end every table, in this order (Track::resolve reads them from the end): the track's Level, the mixer
    // strip's Pan, the insert's four knobs and the strip's three sends. Appended last: saved destination indices stay valid.
    makeTarget("amp.level", "level", "Level", "Amp"),
    makeTarget("amp.pan", "pan", "Pan", "Amp"),
    makeTarget("insert.p1", "insP1", "Insert P1", "Insert"),
    makeTarget("insert.p2", "insP2", "Insert P2", "Insert"),
    makeTarget("insert.p3", "insP3", "Insert P3", "Insert"),
    makeTarget("insert.p4", "insP4", "Insert P4", "Insert"),
    makeTarget("send.effect", "sendEff", "Effect Send", "Effects"),
    makeTarget("send.delay", "sendDly", "Delay Send", "Effects"),
    makeTarget("send.reverb", "sendRev", "Reverb Send", "Effects"),
};

// FM Perc — order matches FmPercEngine::setParams(tune, ratio, index, idec, fdbk, dive, dtime, decay, fold, noise).
// The Algorithm is a choice, not a target. Every row but Tune and Ratio is a normalised amount, so a depth means the
// same musical share of the knob in every algorithm.
inline constexpr ModDestEntry kFmPercDests[] = {
    makeTarget("fp.tune.prop", "fmp_tune", "Tune", "FM Perc"),
    makeTarget("fp.ratio.prop", "fmp_ratio", "Ratio", "FM Perc"),
    makeTarget("fp.index.prop", "fmp_index", "Index", "FM Perc"),
    makeTarget("fp.idec.prop", "fmp_idec", "FM Env", "FM Perc"),
    makeTarget("fp.fdbk.prop", "fmp_fdbk", "Feedback", "FM Perc"),
    makeTarget("fp.dive.prop", "fmp_dive", "Dive", "FM Perc"),
    makeTarget("fp.dtime.prop", "fmp_dtime", "Dive Time", "FM Perc"),
    makeTarget("fp.decay.prop", "fmp_decay", "Decay", "FM Perc"),
    makeTarget("fp.fold.prop", "fmp_fold", "Fold", "FM Perc"),
    makeTarget("fp.noise.prop", "fmp_noise", "Noise", "FM Perc"),
    // A drum or percussion table has six more rows just before those nine (Track::resolve reads them from the end): the
    // filter's Cutoff, Resonance, Env Attack, Env Decay and Env Depth, then the track's Ghost level.
    makeTarget("filter.cut", "fmp_fcut", "Filter Cutoff", "Filter"),
    makeTarget("filter.res", "fmp_fres", "Filter Resonance", "Filter"),
    makeTarget("filter.atk", "fmp_fatk", "Filter Env Attack", "Filter"),
    makeTarget("filter.dec", "fmp_fdec", "Filter Env Decay", "Filter"),
    makeTarget("filter.dep", "fmp_fdep", "Filter Env Depth", "Filter"),
    makeTarget("groove.ghost", "ghost", "Ghost", "Groove"),
    // The same nine rows end every table, in this order (Track::resolve reads them from the end): the track's Level, the mixer
    // strip's Pan, the insert's four knobs and the strip's three sends. Appended last: saved destination indices stay valid.
    makeTarget("amp.level", "level", "Level", "Amp"),
    makeTarget("amp.pan", "pan", "Pan", "Amp"),
    makeTarget("insert.p1", "insP1", "Insert P1", "Insert"),
    makeTarget("insert.p2", "insP2", "Insert P2", "Insert"),
    makeTarget("insert.p3", "insP3", "Insert P3", "Insert"),
    makeTarget("insert.p4", "insP4", "Insert P4", "Insert"),
    makeTarget("send.effect", "sendEff", "Effect Send", "Effects"),
    makeTarget("send.delay", "sendDly", "Delay Send", "Effects"),
    makeTarget("send.reverb", "sendRev", "Reverb Send", "Effects"),
};

// Chord — order matches ChordEngine::setParams(wave, cutHz, res, drv, env, edecMs, atkMs, decMs, sus, relMs, ...); the wave is a
// choice, not a target.
inline constexpr ModDestEntry kChordDests[] = {
    makeTarget("c.cut.prop", "chord_cut", "Cutoff", "Chord"),
    makeTarget("c.res.prop", "chord_res", "Resonance", "Chord"),
    makeTarget("c.drive.prop", "chord_drive", "Drive", "Chord"),
    makeTarget("c.env.prop", "chord_env", "Filter Env", "Chord"),
    makeTarget("c.edec.prop", "chord_edec", "Env Decay", "Chord"),
    makeTarget("c.atk.prop", "chord_atk", "Attack", "Chord"),
    makeTarget("c.dec.prop", "chord_dec", "Decay", "Chord"),
    makeTarget("c.sus.prop", "chord_sus", "Sustain", "Chord"),
    makeTarget("c.rel.prop", "chord_rel", "Release", "Chord"),
    // The same nine rows end every table, in this order (Track::resolve reads them from the end): the track's Level, the mixer
    // strip's Pan, the insert's four knobs and the strip's three sends. Appended last: saved destination indices stay valid.
    makeTarget("amp.level", "level", "Level", "Amp"),
    makeTarget("amp.pan", "pan", "Pan", "Amp"),
    makeTarget("insert.p1", "insP1", "Insert P1", "Insert"),
    makeTarget("insert.p2", "insP2", "Insert P2", "Insert"),
    makeTarget("insert.p3", "insP3", "Insert P3", "Insert"),
    makeTarget("insert.p4", "insP4", "Insert P4", "Insert"),
    makeTarget("send.effect", "sendEff", "Effect Send", "Effects"),
    makeTarget("send.delay", "sendDly", "Delay Send", "Effects"),
    makeTarget("send.reverb", "sendRev", "Reverb Send", "Effects"),
};

// Compile-time guard: every row of an engine's table names a parameter under that engine's tag
// ("kick_..."), so a rename that misses a literal above fails the build instead of silencing a destination.
// The closing rows every table shares (and a drum or percussion table's Ghost row): the track's Level and insert knobs (t{N}_) and the mixer strip's pan and sends (ch{N}_).
constexpr bool isLayerWideParam(std::string_view p) noexcept
{
    return p == "level" || p == "ghost" || p == "insP1" || p == "insP2" || p == "insP3" || p == "insP4" || mu_mod::isMixerStripParam(p);
}

template <std::size_t N>
constexpr bool allUnderPrefix(const ModDestEntry (&table)[N], const char* prefix) noexcept
{
    for (const auto& row : table)
        for (std::size_t c = 0; prefix[c] != '\0'; ++c)
            if (! isLayerWideParam(row.param) && row.param[c] != prefix[c]) return false;   // a shorter param also stops here, at its terminator; the nine closing rows are not under the engine's tag
    return true;
}
static_assert(allUnderPrefix(kKickDests,   "kick_"),   "Kick targets must use the kick_ parameters");
static_assert(allUnderPrefix(kBassDests,   "bass_"),   "Bass targets must use the bass_ parameters");
static_assert(allUnderPrefix(kHatDests,    "hat_"),    "Hat targets must use the hat_ parameters");
static_assert(allUnderPrefix(kSnareDests,  "snare_"),  "Snare targets must use the snare_ parameters");
static_assert(allUnderPrefix(kRumbleDests, "rumble_"), "Rumble targets must use the rumble_ parameters");
static_assert(allUnderPrefix(kFmPercDests, "fmp_"),    "FM Perc targets must use the fmp_ parameters");
static_assert(allUnderPrefix(kChordDests,  "chord_"),  "Chord targets must use the chord_ parameters");

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
        case EngineType::Chord:  count = (int) std::size(kChordDests);  return kChordDests;
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
