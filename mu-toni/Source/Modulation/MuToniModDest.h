#pragma once

#include "UI/ModulatorEditor.h"           // mu-core: ModDestProvider + wireTableModDestResolve
#include "UI/Components/DropdownSelect.h"
#include "Modulation/ModulationMatrix.h"  // mu-core: registerDepthScale

#include <array>
#include <string>
#include <mutex>

// μ-Toni's modulation-destination registry + provider. Mirrors mu-tant's
// MuTantModDest.h: each destination maps a stable string `id` (used in saved
// assignments + the audio-thread paramValues map) to a dropdown alias + section.
// All dests are resolved through the shared mu_mod::resolveLane in PROPORTION
// space, so every depth scale is 1.0 (full-depth mod sweeps the whole range).
// Order MUST match the D_* enum below + the modDestRanges built in the processor.
namespace mu_toni
{

struct ModDest { const char* id; const char* alias; const char* section; };

inline constexpr ModDest kModDestTable[] = {
    // ── Arpeggiator ───────────────────────────────────────────────────────────
    { "arp.dir",    "Direction",  "Arp"    },
    { "arp.octs",   "Octaves",    "Arp"    },
    { "arp.inv",    "Inversion",  "Arp"    },
    { "arp.chord",  "Chord",      "Arp"    },
    { "arp.root",   "Root",       "Arp"    },
    { "arp.roct",   "Root Octave","Arp"    },
    { "arp.rate",   "Rate",       "Arp"    },
    { "arp.gate",   "Gate Length","Arp"    },
    { "arp.porta",  "Portamento", "Arp"    },
    // ── Oscillators ───────────────────────────────────────────────────────────
    { "osc1.level", "Osc1 Level", "Osc"    },
    { "osc2.level", "Osc2 Level", "Osc"    },
    { "osc2.semi",  "Osc2 Semi",  "Osc"    },
    { "osc.pw",     "Pulse Width","Osc"    },
    { "noise.level","Noise",      "Osc"    },
    // ── Filter ────────────────────────────────────────────────────────────────
    { "flt.cutoff", "Cutoff",     "Filter" },
    { "flt.res",    "Resonance",  "Filter" },
    { "flt.drive",  "Drive",      "Filter" },
    { "flt.env",    "Env Depth",  "Filter" },
    // ── Envelopes ─────────────────────────────────────────────────────────────
    { "amp.level",  "Amp Level",  "Amp"    },
    { "pitch.env",  "Pitch Env",  "Pitch"  },
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

// Register every dest with a full-swing depth scale of 1.0 (proportion space).
// Idempotent + thread-safe; call once from the PluginProcessor ctor.
inline void registerDepthScales()
{
    static std::once_flag once;
    std::call_once(once, []
    {
        for (const auto& d : kModDestTable)
            ModulationMatrix::registerDepthScale(d.id, 1.0f);
    });
}

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
        const char* currentSection = nullptr;
        for (int i = 0; i < kNumModDests; ++i)
        {
            if (currentSection == nullptr || std::strcmp(currentSection, kModDestTable[i].section) != 0)
            {
                currentSection = kModDestTable[i].section;
                dd.addSectionHeading(currentSection);
            }
            dd.addItem(kModDestTable[i].alias, i + 1);   // 1-based id = table index + 1
        }
    };

    wireTableModDestResolve(p,
        [](int i) { return std::string(kModDestTable[i].id); },
        kNumModDests);

    return p;
}

} // namespace mu_toni
