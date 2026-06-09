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
    // ── Pitch (osc2) ────────────────────────────────────────────────────────
    { "osc2.octave",  "Osc2 Octave",   "Osc 2" },
    { "osc2.semi",    "Osc2 Semi",     "Osc 2" },
    { "osc2.fine",    "Osc2 Fine",     "Osc 2" },
    { "osc2.pos",     "Osc2 Position", "Osc 2" },
    // ── Cross-mod ─────────────────────────────────────────────────────────────
    { "xmod.fm",      "FM Depth",      "X-Mod" },
    { "xmod.am",      "AM Depth",      "X-Mod" },
    { "xmod.ring",    "Ring Depth",    "X-Mod" },
    // ── Levels ────────────────────────────────────────────────────────────────
    { "osc1.level",   "Osc1 Level",    "Levels" },
    { "osc2.level",   "Osc2 Level",    "Levels" },
    { "noise.level",  "Noise Level",   "Levels" },
    // ── Filter ──────────────────────────────────────────────────────────────
    { "filter.cutoff",     "Filter Cutoff",     "Filter" },
    { "filter.resonance",  "Filter Resonance",  "Filter" },
    // ── Filter 2 (proportion-space — ".prop" → depthScaleFor=1.0, no mu-core edit) ──
    { "filter2.cutoff.prop",    "Filter 2 Cutoff",    "Filter 2" },
    { "filter2.resonance.prop", "Filter 2 Resonance", "Filter 2" },
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

inline ModDestProvider makeModDestProvider()
{
    ModDestProvider p;

    p.populate = [](DropdownSelect& dd, int driveChar)
    {
        // Walk the table once, opening a new section heading whenever the
        // section string changes. Items use the table index + 1 as their
        // 1-based dropdown ID so saved assignments can be reverse-resolved.
        // The Insert section uses per-algo slot labels when an algo is active.
        const char* currentSection = nullptr;
        for (int i = 0; i < kModDestCount; ++i)
        {
            const bool isInsert = (std::strcmp(kModDestTable[i].section, "Insert") == 0);

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
