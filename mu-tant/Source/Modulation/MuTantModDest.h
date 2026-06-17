#pragma once

#include "UI/ModulatorEditor.h"           // mu-core: ModDestProvider
#include "UI/Components/DropdownSelect.h"
#include "Audio/AlgorithmNames.h"         // mu-core: kInsertAlgorithmNames
#include "Audio/InsertSlotConfig.h"        // mu-core: kInsertAlgoSlots / kInsertSlotCount
#include "Modulation/ModulationMatrix.h"  // mu-core: registerDepthScale

#include <array>
#include <cstring>
#include <mutex>

// mu-tant's modulation-destination registry + provider.
//
// Each entry has:
//   - id:    the stable string ID used in saved assignments + the audio-thread
//            paramValues map (e.g. "filter.cutoff", "osc1.pos").
//   - alias: the human-friendly label shown in the destination dropdown.
//
// New destinations MUST be appended to the end. The 1-based dropdown indices
// are persisted across UI sessions; saved .muTant assignments reference the
// string `id`, so reordering this table only scrambles dropdown IDs of
// already-open editors — saved presets survive.
namespace mu_tant
{

struct ModDest { const char* id; const char* alias; const char* section; };

inline constexpr ModDest kModDestTable[] = {
    // ── Pitch (osc1) ────────────────────────────────────────────────────────
    { "osc1.octave",  "Osc1 Octave",   "Osc 1" },
    { "osc1.semi",    "Osc1 Semi",     "Osc 1" },
    { "osc1.fine",    "Osc1 Fine",     "Osc 1" },
    { "osc1.pos",     "Osc1 Position", "Osc 1" },
    { "osc1.penv.prop", "Pitch Env",   "Osc 1" },
    // ── Pitch (osc2) ────────────────────────────────────────────────────────
    { "osc2.octave",  "Osc2 Octave",   "Osc 2" },
    { "osc2.semi",    "Osc2 Semi",     "Osc 2" },
    { "osc2.fine",    "Osc2 Fine",     "Osc 2" },
    { "osc2.pos",     "Osc2 Position", "Osc 2" },
    { "osc2.penv.prop", "Pitch Env",   "Osc 2" },
    // ── Cross-mod (2-lane bus model — mu-tant-xmod-design.md) ─────────────────
    { "xmod.index",   "X-Mod Index",   "X-Mod" },
    { "xmod.depth",   "X-Mod Depth",   "X-Mod" },
    { "xmod.ssb",     "X-Mod SSB",     "X-Mod" },
    // ── Levels ────────────────────────────────────────────────────────────────
    { "osc1.level",   "Osc1 Level",    "Levels" },
    { "osc2.level",   "Osc2 Level",    "Levels" },
    { "noise.level",  "Noise Level",   "Levels" },
    // ── Filter 1 (cutoff/resonance are shared mu-core dests; drive/lo-cut use the
    //    ".prop" proportion convention → depthScaleFor=1.0, no mu-core edit) ──────
    { "filter.cutoff",     "Cutoff",     "Filter 1" },
    { "filter.resonance",  "Resonance",  "Filter 1" },
    { "filter.drive.prop", "Drive",      "Filter 1" },
    { "filter.locut.prop", "Low Cut",    "Filter 1" },
    { "filter.env.prop",   "Env Depth",  "Filter 1" },
    // ── Filter 2 (proportion-space — ".prop" → depthScaleFor=1.0, no mu-core edit) ──
    { "filter2.cutoff.prop",    "Cutoff",     "Filter 2" },
    { "filter2.resonance.prop", "Resonance",  "Filter 2" },
    { "filter2.drive.prop",     "Drive",      "Filter 2" },
    { "filter2.locut.prop",     "Low Cut",    "Filter 2" },
    { "filter2.env.prop",       "Env Depth",  "Filter 2" },
    // ── Amp ─────────────────────────────────────────────────────────────────
    { "level",        "Level",         "Amp"    },
    // ── Insert (normalised 0..1 — same IDs as mu-clid so depthScaleFor=1.0) ──
    { "insert.p1",    "Insert P1",     "Insert" },
    { "insert.p2",    "Insert P2",     "Insert" },
    { "insert.p3",    "Insert P3",     "Insert" },
    { "insert.p4",    "Insert P4",     "Insert" },
};

inline constexpr int kModDestCount = (int) (sizeof(kModDestTable) / sizeof(kModDestTable[0]));

// Register mu-tant's engine-specific destination depth scales with mu-core (so mu-core no
// longer enumerates mu-tant param ids). Idempotent + thread-safe via call_once; call from
// the PluginProcessor ctor so it runs once on the message thread before any audio reads the
// scales.
//
// These dests are now routed through the shared mu_mod::resolveLane helper, which seeds each
// in PROPORTION space (slider NormalisableRange → 0..1) and writes the modulated value back in
// the param's own units — so every scale is 1.0 (a full-depth mod sweeps the whole range and
// clamps at the rails, matching mu-on + the `.prop` convention). The previous display-unit
// scales (osc=6/24/200/255, level=66) equalled each param's range width, so the modulation
// magnitude is unchanged; only the rail-clamping is new. The registration still overrides the
// mu-core 0..100 default of 100 (the ids don't end in `.prop`, so they need an explicit entry).
// `filter.cutoff`/`filter.resonance` are shared mu-core dests (1.0 / 0.99) — left untouched here
// so a co-loaded mu-clid keeps its scales; mu-tant's `.prop` dests need no entry.
inline void registerDepthScales()
{
    static std::once_flag once;
    std::call_once(once, []
    {
        for (const char* id : { "osc1.octave", "osc2.octave", "osc1.semi", "osc2.semi",
                                "osc1.fine",   "osc2.fine",   "osc1.pos",  "osc2.pos",
                                "osc1.level",  "osc2.level",  "noise.level", "level" })
            ModulationMatrix::registerDepthScale(id, 1.0f);
    });
}

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
        // Walk the table once, opening a new section heading whenever the
        // section string changes. Items use the table index + 1 as their
        // 1-based dropdown ID so saved assignments can be reverse-resolved.
        // The Insert section uses per-algo slot labels when an algo is active.
        const char* currentSection = nullptr;
        for (int i = 0; i < kModDestCount; ++i)
        {
            // In Smooth mode, omit stepped-only destinations (octave) — the IDs are the
            // table index +1, so skipping an item leaves the others' IDs unchanged.
            if (! steppedMode && isSteppedOnlyDest(kModDestTable[i].id)) continue;

            const bool isInsert = (std::strcmp(kModDestTable[i].section, "Insert") == 0);

            // No insert algorithm selected → hide the Insert section + its P1-P4 targets
            // entirely (mirrors mu-clid, which only adds them when driveChar > 0).
            if (isInsert && driveChar <= 0) continue;

            if (currentSection == nullptr || std::strcmp(currentSection, kModDestTable[i].section) != 0)
            {
                currentSection = kModDestTable[i].section;
                // For the Insert section: open with the algo name when one is active.
                if (isInsert)
                {
                    if (driveChar > 0
                        && driveChar < (int) std::size(mu_audio::kInsertAlgorithmNames) - 1)
                        dd.addSectionHeading(mu_audio::kInsertAlgorithmNames[driveChar]);
                    else
                        dd.addSectionHeading("Insert");
                }
                else
                {
                    dd.addSectionHeading(currentSection);
                }
            }

            if (isInsert)
            {
                // Use the per-algo slot label when available, otherwise the generic alias.
                const int slot = i - (kModDestCount - 4);   // 0..3 for the 4 insert destinations
                const char* label = kModDestTable[i].alias;
                if (driveChar > 0
                    && driveChar < (int) std::size(mu_audio::kInsertAlgorithmNames) - 1
                    && slot >= 0 && slot < mu_ui::kInsertSlotCount)
                {
                    const auto& sl = mu_ui::kInsertAlgoSlots[driveChar][slot];
                    if (sl.label != nullptr) label = sl.label;
                    else continue;   // hidden slot → skip
                }
                dd.addItem(label, i + 1);
            }
            else
            {
                dd.addItem(kModDestTable[i].alias, i + 1);
            }
        }
    };

    wireTableModDestResolve(p,
        [](int i) { return std::string(kModDestTable[i].id); },
        kModDestCount);

    return p;
}

} // namespace mu_tant
