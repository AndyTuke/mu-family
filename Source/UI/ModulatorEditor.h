#pragma once
#include <atomic>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Components/SegmentControl.h"
#include "Components/NudgeInput.h"
#include "Components/LFOEditor.h"
#include "Components/StepEditor.h"
#include "Components/DropdownSelect.h"
#include "Components/AddButton.h"
#include "Components/MuClidLookAndFeel.h"
#include "../Sequencer/ControlSequence.h"
#include "../Modulation/ModulationMatrix.h"
#include "../Modulation/ModulationAssignment.h"

// Modulation destination table. Each entry has a stable ID (used to persist assignments)
// and an alias (friendly name shown in the dropdown).
// Dropdown item IDs are always 1-based indices into the table so saved assignments remain
// valid when the insert algorithm or other entries change.
// New destinations are ALWAYS appended to the end to preserve preceding indices.
namespace ModDest
{
    struct Dest { const char* id; const char* alias; };

    inline const Dest kTable[] = {
        // ── Amp (idx 0–3, DD IDs 1–4) ────────────────────────────────────────
        { "amp.attack",       "Amp Attack"         },
        { "amp.decay",        "Amp Decay"          },
        { "amp.sustain",      "Amp Sustain"        },
        { "amp.release",      "Amp Release"        },
        // ── Filter (idx 4–8, DD IDs 5–9) ─────────────────────────────────────
        { "filter.cutoff",    "Filter Cutoff"      },
        { "filter.resonance", "Filter Resonance"   },
        { "fenv.attack",      "Filter Env Attack"  },
        { "fenv.decay",       "Filter Env Decay"   },
        { "fenv.depth",       "Filter Env Depth"   },
        // ── Pitch (idx 9, DD ID 10) ───────────────────────────────────────────
        { "pitch.semitones",  "Pitch Semitones"    },
        // ── Insert (idx 10–15, DD IDs 11–16) ─────────────────────────────────
        { "insert.drive",     "Insert Drive"       },
        { "insert.output",    "Insert Output"      },
        { "insert.bits",      "Insert Bits"        },
        { "insert.rate",      "Insert Rate"        },
        { "insert.dither",    "Insert Dither"      },
        { "insert.lpf",       "Insert LPF"         },
        // ── Euclid A/B pattern (idx 16–19, DD IDs 17–20) ────────────────────
        { "euclid.a.hits",    "Euclid A Hits"      },
        { "euclid.a.rotate",  "Euclid A Rotate"    },
        { "euclid.b.hits",    "Euclid B Hits"      },
        { "euclid.b.rotate",  "Euclid B Rotate"    },
        // ── Pitch octave (idx 20) + fine (idx 21, deprecated) ────────────────
        { "pitch.octave",     "Pitch Octave"       },
        { "pitch.fine",       "Pitch Fine"         },   // deprecated (#218)
        // ── Euclid C pattern (idx 22–23, DD IDs 23–24) ───────────────────────
        { "euclid.c.hits",    "Euclid C Hits"      },
        { "euclid.c.rotate",  "Euclid C Rotate"    },
        // ── #223 additions (idx 24–26, DD IDs 25–27) ─────────────────────────
        { "pitch.envDepth",   "Pitch Env Depth"    },
        { "amp.level",        "Amp Level"          },
        { "accentDb",         "Accent"             },
        // ── Euclid A pad knobs (idx 27–30, DD IDs 28–31) ─────────────────────
        { "euclid.a.prePad",  "Euclid A Pre Pad"       },
        { "euclid.a.postPad", "Euclid A Post Pad"      },
        { "euclid.a.insSt",   "Euclid A Insert Start"  },
        { "euclid.a.insLen",  "Euclid A Insert Length" },
        // ── Euclid B pad knobs (idx 31–34, DD IDs 32–35) ─────────────────────
        { "euclid.b.prePad",  "Euclid B Pre Pad"       },
        { "euclid.b.postPad", "Euclid B Post Pad"      },
        { "euclid.b.insSt",   "Euclid B Insert Start"  },
        { "euclid.b.insLen",  "Euclid B Insert Length" },
        // ── Euclid C pad knobs (idx 35–38, DD IDs 36–39) ─────────────────────
        { "euclid.c.prePad",  "Euclid C Pre Pad"       },
        { "euclid.c.postPad", "Euclid C Post Pad"      },
        { "euclid.c.insSt",   "Euclid C Insert Start"  },
        { "euclid.c.insLen",  "Euclid C Insert Length" },
    };
    static constexpr int kTableSize = (int)(sizeof(kTable) / sizeof(kTable[0]));

    // Populate dd with destinations grouped by section, showing only insert destinations
    // relevant for driveChar (0=None,1=SoftClip,2=HardClip,3=Fold,4=Bitcrusher,5=Clipper,
    // 6=EQ,7=Comp,8=Lim,9=RingMod,10=TapeSat).
    // Uses stable 1-based indices (= table index + 1) so saved assignments survive changes.
    inline void populate(DropdownSelect& dd, int driveChar)
    {
        // Helper: add item using the alias from kTable, with 1-based dropdown ID.
        auto item = [&](int idx) { dd.addItem(kTable[idx].alias, idx + 1); };

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
        // pitch.fine deprecated (#218) — legacy assignments silently no-op.
        dd.addSectionHeading("Pitch");
        item(20);  // Pitch Octave (±3 oct)
        item(9);   // Pitch Semitones (±12 st)
        item(24);  // Pitch Env Depth (#223)

        // ── Filter ────────────────────────────────────────────────────────────
        dd.addSectionHeading("Filter");
        item(4);  item(5);   // Cutoff, Resonance
        item(6);  item(7);  item(8);  // Env Attack, Decay, Depth

        // ── Amp ───────────────────────────────────────────────────────────────
        dd.addSectionHeading("Amp");
        item(25);  // Amp Level (#223)
        item(0);  item(1);  item(2);  item(3);  // Attack, Decay, Sustain, Release
        item(26);  // Accent (#223)

        // ── Insert ────────────────────────────────────────────────────────────
        // Group heading reflects the active algorithm name so the user can see which
        // insert controls are exposed as modulation targets.
        static const char* kInsertNames[] = {
            nullptr,        // 0 = None
            "Soft Clip",    // 1
            "Hard Clip",    // 2
            "Fold",         // 3
            "Bitcrusher",   // 4
            "Clipper",      // 5
            "3-Band EQ",    // 6
            "Compressor",   // 7
            "Limiter",      // 8
        };
        switch (driveChar)
        {
            case 1: case 2: case 3:  // Soft Clip / Hard Clip / Fold
            case 5:                  // Clipper — same drive/output/lpf knob mapping
                dd.addSectionHeading(kInsertNames[driveChar]);
                item(10);  item(11);  item(15);  // Drive, Output, LPF
                break;
            case 4:  // Bitcrusher
                dd.addSectionHeading("Bitcrusher");
                item(12);  item(13);  item(14);  item(15);  // Bits, Rate, Dither, LPF
                break;
            case 6: case 7: case 8:  // EQ / Compressor / Limiter — no mod destinations yet
            default: break;          // None — no insert params
        }
    }
}

// Editor panel for one ControlSequence.
// Layout: mode/polarity header | LFO or Step editor | loop (+ step) timing | assignment list | add button
class ModulatorEditor : public juce::Component
{
public:
    ModulatorEditor();

    // modLock must be the Rhythm::modLock for the owning rhythm.
    // All mutations to cs/matrix are performed under this lock.
    void setData(ControlSequence* cs, ModulationMatrix* matrix, juce::Colour modColour, int index,
                 std::atomic<bool>* modLock = nullptr);

    std::function<void()> onChange;

    // Update the insert algorithm filter so only relevant destinations appear in rows.
    void setInsertAlgorithm(int driveChar);

    // Drive the playhead: pass the current song beat position.
    // Phase is computed as fmod(beat / loopBeats, 1.0).
    void setPlayheadBeat(double beat);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    ControlSequence*   cs       = nullptr;
    ModulationMatrix*  matrix   = nullptr;
    std::atomic<bool>* rhythmModLock = nullptr;
    juce::Colour       modColour;
    int                modIndex = 0;

    // Acquire Rhythm::modLock (spin). Call before any mutation of cs or matrix.
    void lockMod();
    // Release Rhythm::modLock.
    void unlockMod();

    DropdownSelect modeDropdown;    // Smooth / Stepped (#157)
    SegmentControl polarityCtrl{ {"Uni","Bi"} };  // moved below editor (#156)
    LFOEditor      lfoEditor;
    StepEditor     stepEditor;
    DropdownSelect loopDropdown;
    juce::Label    loopLabel;
    // #239: explicit fromUTF8 so the "×" glyph decodes correctly. The implicit
    // char* → juce::String conversion was rendering as garbled Latin-1 pairs.
    NudgeInput     loopMult { juce::String::fromUTF8("\xc3\x97"), 1, 16, 1 };
    DropdownSelect stepDropdown;
    juce::Label    stepLabel;
    NudgeInput     stepMult { juce::String::fromUTF8("\xc3\x97"), 1, 16, 1 };   // #239

    struct AssignmentRow : public juce::Component
    {
        DropdownSelect   destCombo;
        juce::Slider     depthSlider;
        // #224 Bitwig-style bipolar curve knob: -100..+100 = log..linear..exp
        juce::Slider     curveSlider;
        juce::TextButton removeBtn { "x" };
        std::string      id;

        std::function<void()>                         onRemove;
        std::function<void(const std::string& dest)>  onDestChange;
        std::function<void(float depth)>              onDepthChange;
        std::function<void(float curve)>              onCurveChange;

        int rowNumber = 0;

        AssignmentRow(const std::string& assignId, int driveChar);
        void resized() override;
        void paint(juce::Graphics& g) override;
    };

    std::vector<std::unique_ptr<AssignmentRow>> rows;
    juce::Component rowsBox;
    juce::Viewport  rowsViewport;
    juce::TextButton rowPrevBtn { juce::String::charToString(0x25B2) }; // ▲
    juce::TextButton rowNextBtn { juce::String::charToString(0x25BC) }; // ▼
    juce::Label      rowPageLabel;
    AddButton addBtn { "Target" };

    static constexpr int kHeaderH = 28;
    static constexpr int kEditorH = 150;  // increased from 100; timing controls moved to header row
    static constexpr int kTimingH = 28;   // kept for step-count info
    static constexpr int kRowH    = 26;
    static constexpr int kAddBtnH = 28;
    static constexpr int kPagerH  = 20;

    int currentDriveChar = 0;

    void wireHeader();
    void wireTiming();
    void rebuildRows();
    void addTarget();
    void loadFromCS();
    void syncStepValues();
    void updateRowPager();
    void scrollRowPage(int delta);
    void updateStepQuantization();
};
