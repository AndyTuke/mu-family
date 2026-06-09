#pragma once

#include "UI/ModulatorEditor.h"                     // mu-core: ModDestProvider
#include "UI/Components/DropdownSelect.h"
#include "Modulation/ModulationDestinations.h"      // mu-clid kTable
#include "Modulation/ModulationMatrix.h"            // mu-core: registerDepthScale
#include "Audio/InsertSlotConfig.h"                 // kInsertAlgoSlots
#include "Audio/AlgorithmNames.h"                   // kInsertAlgorithmNames

#include <mutex>
#include <string>

// mu-clid's modulation-destination provider — drives the ModulatorEditor /
// ModMatrixPanel destination dropdowns. Lifted out of the inline header
// definition so mu-core's ModulatorEditor no longer depends on mu-clid's
// kTable. The provider's three callbacks share the kTable as their single
// source of truth so a renamed entry stays consistent across populate /
// resolveId / findDropdownId.

namespace mu_clid
{

// Register mu-clid's engine-specific destination depth scales with mu-core (so mu-core no
// longer enumerates mu-clid param ids). Idempotent + thread-safe via call_once; call from
// the PluginProcessor ctor so it runs once on the message thread before any audio reads the
// scales. Euclid hits/rotate/insSt use scale 1.0 (seed/write-back convert via proportion).
inline void registerDepthScales()
{
    static std::once_flag once;
    std::call_once(once, []
    {
        using MM = ModulationMatrix;
        MM::registerDepthScale("ks.note",   6.0f);
        MM::registerDepthScale("ks.octave", 3.0f);
        MM::registerDepthScale("voc.note",  6.0f);
        MM::registerDepthScale("voc.octave", 4.0f);
        MM::registerDepthScale("voc.unison", 6.0f);
        for (const std::string ch : { "a", "b", "c" })
        {
            MM::registerDepthScale("euclid." + ch + ".prePad",  12.0f);
            MM::registerDepthScale("euclid." + ch + ".postPad", 12.0f);
            MM::registerDepthScale("euclid." + ch + ".insLen",   8.0f);
            MM::registerDepthScale("euclid." + ch + ".hits",     1.0f);
            MM::registerDepthScale("euclid." + ch + ".rotate",   1.0f);
            MM::registerDepthScale("euclid." + ch + ".insSt",    1.0f);
        }
    });
}

inline ModDestProvider makeModDestProvider()
{
    ModDestProvider p;

    p.populate = [](DropdownSelect& dd, int driveChar)
    {
        // Helper: add item using the alias from kTable, with 1-based dropdown ID.
        auto item = [&](int idx) { dd.addItem(ModDest::kTable[idx].alias, idx + 1); };

        // ── Euclid A ──────────────────────────────────────────────────────────
        dd.addSectionHeading("Euclid A");
        item(16);  item(17);  // Hits, Rotate
        item(27);  item(28);  // Pre Pad, Post Pad
        item(29);  item(30);  // Insert Start, Insert Length

        // ── Euclid B ──────────────────────────────────────────────────────────
        dd.addSectionHeading("Euclid B");
        item(18);  item(19);
        item(31);  item(32);
        item(33);  item(34);

        // ── Euclid C ──────────────────────────────────────────────────────────
        dd.addSectionHeading("Euclid C");
        item(22);  item(23);
        item(35);  item(36);
        item(37);  item(38);

        // ── Pitch ─────────────────────────────────────────────────────────────
        // pitch.octave: ±3 octaves full swing (scale=36 semitones).
        // pitch.semitones: ±12 semitones full swing. Combined max ±48 st.
        dd.addSectionHeading("Pitch");
        item(20);  // Pitch Octave (±3 oct)
        item(9);   // Pitch Semitones (±12 st)
        item(24);  // Pitch Env Depth

        // ── Filter ────────────────────────────────────────────────────────────
        dd.addSectionHeading("Filter");
        item(4);  item(5);   // Cutoff, Resonance
        item(6);  item(7);  item(8);  // Env Attack, Decay, Depth
        item(44);  // Low Cut

        // ── Amp ───────────────────────────────────────────────────────────────
        dd.addSectionHeading("Amp");
        item(25);  // Amp Level
        item(0);  item(1);  item(2);  // Attack, Decay, Sustain
        // Amp Release (idx 3) is intentionally NOT a modulation target.
        item(26);  // Accent

        // ── Insert ────────────────────────────────────────────────────────────
        // Post-Stage-36: the 4 insert.p1..p4 destinations cover every algorithm; the
        // visible slots + their per-algo labels come from mu_ui::kInsertAlgoSlots.
        // Items added here keep the SAME 1-based table ID (11..14) so saved
        // assignments persist across algorithm changes — the dropdown text just
        // re-labels them. Hidden slots (label == nullptr) are skipped.
        if (driveChar > 0 && driveChar < (int) std::size(mu_audio::kInsertAlgorithmNames) - 1
            && driveChar < 14)
        {
            const auto& slots = mu_ui::kInsertAlgoSlots[driveChar];
            bool addedHeading = false;
            for (int slot = 0; slot < mu_ui::kInsertSlotCount; ++slot)
            {
                if (slots[slot].label == nullptr) continue;
                if (! addedHeading)
                {
                    dd.addSectionHeading(mu_audio::kInsertAlgorithmNames[driveChar]);
                    addedHeading = true;
                }
                // ID = 10 + slot + 1 = 11..14 (1-based table index for insert.pN).
                dd.addItem(slots[slot].label, 10 + slot + 1);
            }
        }
    };

    wireTableModDestResolve(p,
        [](int i) { return std::string(ModDest::kTable[i].id); },
        ModDest::kTableSize);

    return p;
}

} // namespace mu_clid
