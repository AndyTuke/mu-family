#pragma once

#include <cstddef>
#include <cstring>
#include <string>

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
// saved .muRhyth / .muclid assignments reference the string `id`, so they
// survive reordering of the table itself.

namespace ModDest
{
    struct Dest { const char* id; const char* alias; };

    inline const Dest kTable[] = {
        // ── Amp (idx 0–3) ─────────────────────────────────────────────────────
        { "amp.attack",       "Amp Attack"         },
        { "amp.decay",        "Amp Decay"          },
        { "amp.sustain",      "Amp Sustain"        },
        { "amp.release",      "Amp Release"        },
        // ── Filter (idx 4–8) ──────────────────────────────────────────────────
        { "filter.cutoff",    "Filter Cutoff"      },
        { "filter.resonance", "Filter Resonance"   },
        { "fenv.attack",      "Filter Env Attack"  },
        { "fenv.decay",       "Filter Env Decay"   },
        { "fenv.depth",       "Filter Env Depth"   },
        // ── Pitch (idx 9) ─────────────────────────────────────────────────────
        { "pitch.semitones",  "Pitch Semitones"    },
        // ── Insert (idx 10–15) ────────────────────────────────────────────────
        { "insert.drive",     "Insert Drive"       },
        { "insert.output",    "Insert Output"      },
        { "insert.bits",      "Insert Bits"        },
        { "insert.rate",      "Insert Rate"        },
        { "insert.dither",    "Insert Dither"      },
        { "insert.lpf",       "Insert LPF"         },
        // ── Euclid A/B pattern (idx 16–19) ────────────────────────────────────
        { "euclid.a.hits",    "Euclid A Hits"      },
        { "euclid.a.rotate",  "Euclid A Rotate"    },
        { "euclid.b.hits",    "Euclid B Hits"      },
        { "euclid.b.rotate",  "Euclid B Rotate"    },
        // ── Pitch octave (idx 20) ─────────────────────────────────────────────
        // idx 21 ("pitch.fine") was retired in #218 — the slot stays so subsequent
        // kTable indices don't shift (UI dropdown IDs are 1-based table indices).
        // The id below intentionally doesn't match any valid destination string,
        // so `isValidDestinationId("pitch.fine")` returns false and legacy preset
        // assignments are rejected at load.
        { "pitch.octave",     "Pitch Octave"       },
        { "_reserved.pitch.fine", "(reserved)"     },   // formerly "pitch.fine" — #218
        // ── Euclid C pattern (idx 22–23) ──────────────────────────────────────
        { "euclid.c.hits",    "Euclid C Hits"      },
        { "euclid.c.rotate",  "Euclid C Rotate"    },
        // ── #223 additions (idx 24–26) ────────────────────────────────────────
        { "pitch.envDepth",   "Pitch Env Depth"    },
        { "amp.level",        "Amp Level"          },
        { "accentDb",         "Accent"             },
        // ── Euclid A pad knobs (idx 27–30) ────────────────────────────────────
        { "euclid.a.prePad",  "Euclid A Pre Pad"       },
        { "euclid.a.postPad", "Euclid A Post Pad"      },
        { "euclid.a.insSt",   "Euclid A Insert Start"  },
        { "euclid.a.insLen",  "Euclid A Insert Length" },
        // ── Euclid B pad knobs (idx 31–34) ────────────────────────────────────
        { "euclid.b.prePad",  "Euclid B Pre Pad"       },
        { "euclid.b.postPad", "Euclid B Post Pad"      },
        { "euclid.b.insSt",   "Euclid B Insert Start"  },
        { "euclid.b.insLen",  "Euclid B Insert Length" },
        // ── Euclid C pad knobs (idx 35–38) ────────────────────────────────────
        { "euclid.c.prePad",  "Euclid C Pre Pad"       },
        { "euclid.c.postPad", "Euclid C Post Pad"      },
        { "euclid.c.insSt",   "Euclid C Insert Start"  },
        { "euclid.c.insLen",  "Euclid C Insert Length" },
        // ── Algorithm-specific insert destinations (idx 39–43) ────────────────
        // (Karplus): idx 39, 40
        // (Vocoder): idx 41, 42, 43
        { "ks.note",          "Karplus Note"           },
        { "ks.octave",        "Karplus Octave"         },
        { "voc.note",         "Vocoder Note"           },
        { "voc.octave",       "Vocoder Octave"         },
        { "voc.unison",       "Vocoder Unison"         },
    };
    static constexpr int kTableSize = (int)(sizeof(kTable) / sizeof(kTable[0]));

    // validate that `destId` resolves to a known destination. Returns the
    // dropdown alias (for diagnostic messages) when found, nullptr otherwise.
    inline const char* aliasFor(const std::string& destId) noexcept
    {
        for (const auto& d : kTable)
            if (destId == d.id) return d.alias;
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
