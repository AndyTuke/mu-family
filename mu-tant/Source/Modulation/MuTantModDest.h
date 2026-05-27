#pragma once

#include "UI/ModulatorEditor.h"           // mu-core: ModDestProvider
#include "UI/Components/DropdownSelect.h"

#include <array>
#include <cstring>

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
    { "osc1.tone",    "Osc1 Tone",     "Osc 1" },
    { "osc1.fine",    "Osc1 Fine",     "Osc 1" },
    { "osc1.pos",     "Osc1 Position", "Osc 1" },
    // ── Pitch (osc2) ────────────────────────────────────────────────────────
    { "osc2.octave",  "Osc2 Octave",   "Osc 2" },
    { "osc2.tone",    "Osc2 Tone",     "Osc 2" },
    { "osc2.fine",    "Osc2 Fine",     "Osc 2" },
    { "osc2.pos",     "Osc2 Position", "Osc 2" },
    // ── Cross-mod / balance ─────────────────────────────────────────────────
    { "xmod",         "X-Mod",         "Mix"   },
    { "mix",          "Osc Mix",       "Mix"   },
    // ── Filter ──────────────────────────────────────────────────────────────
    { "filter.cutoff",     "Filter Cutoff",     "Filter" },
    { "filter.resonance",  "Filter Resonance",  "Filter" },
    // ── Amp ─────────────────────────────────────────────────────────────────
    { "level",        "Level",         "Amp"   },
};

inline constexpr int kModDestCount = (int) (sizeof(kModDestTable) / sizeof(kModDestTable[0]));

inline ModDestProvider makeModDestProvider()
{
    ModDestProvider p;

    p.populate = [](DropdownSelect& dd, int /*driveChar*/)
    {
        // Walk the table once, opening a new section heading whenever the
        // section string changes. Items use the table index + 1 as their
        // 1-based dropdown ID so saved assignments can be reverse-resolved.
        const char* currentSection = nullptr;
        for (int i = 0; i < kModDestCount; ++i)
        {
            if (currentSection == nullptr || std::strcmp(currentSection, kModDestTable[i].section) != 0)
            {
                currentSection = kModDestTable[i].section;
                dd.addSectionHeading(currentSection);
            }
            dd.addItem(kModDestTable[i].alias, i + 1);
        }
    };

    p.resolveId = [](int dropdownId) -> std::string
    {
        const int idx = dropdownId - 1;
        if (idx < 0 || idx >= kModDestCount) return {};
        return std::string(kModDestTable[idx].id);
    };

    p.findDropdownId = [](const std::string& destId) -> int
    {
        for (int i = 0; i < kModDestCount; ++i)
            if (destId == kModDestTable[i].id) return i + 1;
        return 0;
    };

    return p;
}

} // namespace mu_tant
