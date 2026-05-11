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

// Pre-APVTS modulation destination list. Replaced by APVTS parameter IDs in Stage 10.
// Item IDs in dropdowns are always 1-based indices into the full ids array so that
// assignments remain valid when the insert algorithm changes.
// New destinations are appended to the end so existing saved assignment IDs are preserved.
namespace ModDest
{
    inline const juce::StringArray ids {
        // ── Amp (0–3) ──────────────────────────────────────────────────────────
        "amp.attack",        "amp.decay",       "amp.sustain",    "amp.release",
        // ── Filter (4–8) ───────────────────────────────────────────────────────
        "filter.cutoff",     "filter.resonance",
        "fenv.attack",       "fenv.decay",      "fenv.depth",
        // ── Pitch (9) — semitone kept at index 9 for backward compat ──────────
        "pitch.semitones",
        // ── Insert (10–15) ────────────────────────────────────────────────────
        "insert.drive",      "insert.output",                      // Soft/Hard/Fold
        "insert.bits",       "insert.rate",     "insert.dither",  // Bitcrusher
        "insert.lpf",                                              // all insert algorithms
        // ── Euclid A/B (16–19) ────────────────────────────────────────────────
        "euclid.a.hits",     "euclid.a.rotate",
        "euclid.b.hits",     "euclid.b.rotate",
        // ── Pitch octave + fine (20–21, appended to preserve IDs) ────────────
        "pitch.octave",      "pitch.fine",
        // ── Euclid C (22–23) ─────────────────────────────────────────────────
        "euclid.c.hits",     "euclid.c.rotate"
    };
    inline const juce::StringArray labels {
        "Amp Attack",        "Amp Decay",         "Amp Sustain",       "Amp Release",
        "Filter Cutoff",     "Filter Resonance",
        "Filter Env Attack", "Filter Env Decay",  "Filter Env Depth",
        "Pitch Semitone",
        "Insert Drive",      "Insert Output",
        "Insert Bits",       "Insert Rate",       "Insert Dither",
        "Insert LPF",
        "Euclid A Hits",     "Euclid A Rotate",
        "Euclid B Hits",     "Euclid B Rotate",
        "Pitch Octave",      "Pitch Fine",
        "Euclid C Hits",     "Euclid C Rotate"
    };

    // Populate dd with destinations grouped by section, showing only insert destinations
    // relevant for driveChar (0=None,1=Soft,2=Hard,3=Fold,4=Bit,5=Clipper,6=EQ,7=Comp,8=Lim).
    // Uses stable 1-based indices so saved assignments survive algorithm changes.
    inline void populate(DropdownSelect& dd, int driveChar)
    {
        // ── Euclid A ──────────────────────────────────────────────────────────
        dd.addSectionHeading("Euclid A");
        dd.addItem("Hits",   17);  // euclid.a.hits
        dd.addItem("Rotate", 18);  // euclid.a.rotate

        // ── Euclid B ──────────────────────────────────────────────────────────
        dd.addSectionHeading("Euclid B");
        dd.addItem("Hits",   19);  // euclid.b.hits
        dd.addItem("Rotate", 20);  // euclid.b.rotate

        // ── Euclid C ──────────────────────────────────────────────────────────
        dd.addSectionHeading("Euclid C");
        dd.addItem("Hits",   23);  // euclid.c.hits
        dd.addItem("Rotate", 24);  // euclid.c.rotate

        // ── Pitch ─────────────────────────────────────────────────────────────
        dd.addSectionHeading("Pitch");
        dd.addItem("Octave",   21);  // pitch.octave
        dd.addItem("Semitone", 10);  // pitch.semitones
        dd.addItem("Fine",     22);  // pitch.fine

        // ── Filter ────────────────────────────────────────────────────────────
        dd.addSectionHeading("Filter");
        dd.addItem("Cutoff",    5);  // filter.cutoff
        dd.addItem("Resonance", 6);  // filter.resonance
        dd.addItem("Env Attack",  7);  // fenv.attack
        dd.addItem("Env Decay",   8);  // fenv.decay
        dd.addItem("Env Depth",   9);  // fenv.depth

        // ── Amp ───────────────────────────────────────────────────────────────
        dd.addSectionHeading("Amp");
        dd.addItem("Attack",  1);  // amp.attack
        dd.addItem("Decay",   2);  // amp.decay
        dd.addItem("Sustain", 3);  // amp.sustain
        dd.addItem("Release", 4);  // amp.release

        // ── Insert ────────────────────────────────────────────────────────────
        // Group heading reflects the active algorithm name so the user can see at a glance
        // which insert controls are exposed as modulation targets.
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
                dd.addItem("Drive",  11);  // insert.drive
                dd.addItem("Output", 12);  // insert.output
                dd.addItem("LPF",    16);  // insert.lpf
                break;
            case 4:  // Bitcrusher
                dd.addSectionHeading("Bitcrusher");
                dd.addItem("Bits",   13);  // insert.bits
                dd.addItem("Rate",   14);  // insert.rate
                dd.addItem("Dither", 15);  // insert.dither
                dd.addItem("LPF",    16);  // insert.lpf
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
    NudgeInput     loopMult { juce::String::fromUTF8("\xc3\x97"), 1, 16, 4 };
    DropdownSelect stepDropdown;
    juce::Label    stepLabel;
    NudgeInput     stepMult { juce::String::fromUTF8("\xc3\x97"), 1, 16, 1 };   // #239

    struct AssignmentRow : public juce::Component
    {
        DropdownSelect   destCombo;
        juce::Slider     depthSlider;
        juce::TextButton removeBtn { "x" };
        std::string      id;

        std::function<void()>                         onRemove;
        std::function<void(const std::string& dest)>  onDestChange;
        std::function<void(float depth)>              onDepthChange;

        AssignmentRow(const std::string& assignId, int driveChar);
        void resized() override;
    };

    std::vector<std::unique_ptr<AssignmentRow>> rows;
    juce::Component rowsBox;
    juce::Viewport  rowsViewport;
    AddButton addBtn { "Target" };

    static constexpr int kHeaderH = 28;
    static constexpr int kEditorH = 100;
    static constexpr int kTimingH = 28;
    static constexpr int kRowH    = 26;
    static constexpr int kAddBtnH = 28;

    int currentDriveChar = 0;

    void wireHeader();
    void wireTiming();
    void rebuildRows();
    void addTarget();
    void loadFromCS();
    void syncStepValues();
};
