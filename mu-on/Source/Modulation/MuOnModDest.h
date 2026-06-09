#pragma once

#include "UI/ModulatorEditor.h"            // mu-core: ModDestProvider
#include "UI/Components/DropdownSelect.h"   // mu-core
#include "Plugin/MuOnChannels.h"           // Channel enum

#include <cstddef>
#include <cstring>
#include <string>

// μ-On modulation-destination registry — one table per FIXED instrument lane
// (Kick / Bass / Hat / Snare). Unlike mu-tant (every voice identical), μ-On's lanes
// expose different engine params, so each lane has its own destination set and the
// editor swaps the ModulatorPanel's destination provider when the lane changes.
//
// Each entry carries:
//   - propId:  the stable destination id stored in saved assignments + the audio-thread
//              paramValues map. ALWAYS ends in ".prop" so mu-core's depthScaleFor treats
//              it as proportion-space (scale 1.0): the engine seeds the slider's 0..1
//              proportion, the matrix offsets it, and the engine converts back via the
//              param's NormalisableRange. A full-depth mod sweeps the whole knob.
//   - apvtsId: the APVTS parameter the proportion is taken from / written back to.
//   - alias:   the human-friendly dropdown label.
//
// The table ORDER is the single source of truth: it equals the order GrooveVoices feeds
// the modulated values back into each engine's setParams(), so reordering a lane's table
// without matching the engine dispatch would scramble the mapping. New dests append.
namespace mu_on
{

struct ModDestEntry { const char* propId; const char* apvtsId; const char* alias; };

// Kick — order matches KickEngine::setParams(baseHz, pitchAmtHz, pitchDecMs, ampDecMs, drv).
inline constexpr ModDestEntry kKickDests[] = {
    { "k.tune.prop",  "k_tune",  "Tune"        },
    { "k.ptch.prop",  "k_ptch",  "Pitch Amt"   },
    { "k.pdec.prop",  "k_pdec",  "Pitch Decay" },
    { "k.adec.prop",  "k_adec",  "Decay"       },
    { "k.drive.prop", "k_drive", "Drive"       },
};

// Bass — order matches BassEngine::setParams(rootHz, [wave], sub, cutHz, res, env,
// edecMs, atkMs, decMs, sus, drv). Wave is a choice param — not modulatable, omitted.
inline constexpr ModDestEntry kBassDests[] = {
    { "b.tune.prop",  "b_tune",  "Tune"       },
    { "b.sub.prop",   "b_sub",   "Sub"        },
    { "b.cut.prop",   "b_cut",   "Cutoff"     },
    { "b.res.prop",   "b_res",   "Resonance"  },
    { "b.env.prop",   "b_env",   "Filter Env" },
    { "b.edec.prop",  "b_edec",  "Env Decay"  },
    { "b.atk.prop",   "b_atk",   "Attack"     },
    { "b.dec.prop",   "b_dec",   "Decay"      },
    { "b.sus.prop",   "b_sus",   "Sustain"    },
    { "b.drive.prop", "b_drive", "Drive"      },
};

// Hat — order matches SampleChannel::setParams(tuneSemitones, decayMs).
inline constexpr ModDestEntry kHatDests[] = {
    { "h.tune.prop", "h_tune", "Tune"  },
    { "h.dec.prop",  "h_dec",  "Decay" },
};

// Snare — same SampleChannel::setParams order.
inline constexpr ModDestEntry kSnareDests[] = {
    { "s.tune.prop", "s_tune", "Tune"  },
    { "s.dec.prop",  "s_dec",  "Decay" },
};

// Rumble — order matches RumbleEngine::setParams(bpm, drive, d1, d2, d3, revSize, revMix, revLpHz, cutHz, res)
// (bpm is the transport, not a dest — the 9 entries below are the modulatable args in order).
inline constexpr ModDestEntry kRumbleDests[] = {
    { "r.drive.prop", "r_drive", "Drive"      },
    { "r.d1.prop",    "r_d1",    "1/16"       },
    { "r.d2.prop",    "r_d2",    "2/16"       },
    { "r.d3.prop",    "r_d3",    "3/16"       },
    { "r.size.prop",  "r_size",  "Rev Size"   },
    { "r.revmix.prop","r_revmix","Rev Mix"    },
    { "r.revlp.prop", "r_revlp", "Rev LP"     },
    { "r.cut.prop",   "r_cut",   "Cutoff"     },
    { "r.res.prop",   "r_res",   "Resonance"  },
};

// The destination table for a lane (Channel enum), plus its entry count.
inline const ModDestEntry* destsForLane(int lane, int& count) noexcept
{
    switch (lane)
    {
        case Kick:  count = (int) std::size(kKickDests);  return kKickDests;
        case Bass:  count = (int) std::size(kBassDests);  return kBassDests;
        case Hat:    count = (int) std::size(kHatDests);    return kHatDests;
        case Snare:  count = (int) std::size(kSnareDests);  return kSnareDests;
        case Rumble: count = (int) std::size(kRumbleDests); return kRumbleDests;
        default:     count = 0;                             return nullptr;
    }
}

// True if `id` is a valid destination for `lane` — used to drop foreign assignments
// on preset load (each VoiceSlot belongs to one lane).
inline bool isValidLaneDest(int lane, const std::string& id) noexcept
{
    int n = 0;
    const ModDestEntry* t = destsForLane(lane, n);
    for (int i = 0; i < n; ++i)
        if (id == t[i].propId) return true;
    return false;
}

// Build the mu-core ModDestProvider for one lane (drives the destination dropdown +
// id resolution in the shared ModulatorPanel). The editor holds one per lane and
// hands the active lane's provider to the panel on selection.
inline ModDestProvider makeModDestProvider(int lane)
{
    ModDestProvider p;

    p.populate = [lane](DropdownSelect& dd, int /*driveChar*/)
    {
        int n = 0;
        const ModDestEntry* t = destsForLane(lane, n);
        for (int i = 0; i < n; ++i)
            dd.addItem(t[i].alias, i + 1);   // 1-based id = table index + 1
    };

    int count = 0;
    destsForLane(lane, count);
    wireTableModDestResolve(p,
        [lane](int i) { int n = 0; return std::string(destsForLane(lane, n)[i].propId); },
        count);

    return p;
}

} // namespace mu_on
