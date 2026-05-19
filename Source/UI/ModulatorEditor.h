#pragma once
#include <atomic>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Components/SegmentControl.h"
#include "Components/NudgeInput.h"
#include "Components/LFOEditor.h"
#include "Components/StepEditor.h"
#include "Components/DropdownSelect.h"
#include "Components/AddButton.h"
#include "Components/BipolarSliderRow.h"
#include "Components/MuClidLookAndFeel.h"
#include "../Sequencer/ControlSequence.h"
#include "../Modulation/ModulationMatrix.h"
#include "../Modulation/ModulationAssignment.h"
#include "../Modulation/ModulationDestinations.h"   // kTable now lives here

namespace ModDest
{
    // kTable + Dest struct moved to Source/Modulation/ModulationDestinations.h
    // so non-UI code (preset deserialiser) can validate assignments against the
    // same single source of truth. UI populate() helper stays here.

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
                break;
            case 11:  // Karplus (#422-followups)
                dd.addSectionHeading("Karplus");
                item(39);  // KS Note    (drives driveDrive 0..6)
                item(40);  // KS Octave  (drives drvBits 0..3)
                item(14);  // Insert Dither = Feedback knob (continuous 0..100%)
                item(15);  // Insert LPF  = damping cutoff
                break;
            case 12:  // Vocoder (#423-followups)
                dd.addSectionHeading("Vocoder");
                item(41);  // Voc Note    (drives drvBits offset by +1)
                item(42);  // Voc Octave  (drives drvDither 1..5)
                item(43);  // Voc Unison  (drives encoded driveOutput)
                break;
            default: break;          // None / TapeSat / RingMod — no insert params yet
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
    // explicit fromUTF8 so the "×" glyph decodes correctly. The implicit
    // char* → juce::String conversion was rendering as garbled Latin-1 pairs.
    NudgeInput     loopMult { juce::String::fromUTF8("\xc3\x97"), 1, 16, 1 };
    DropdownSelect stepDropdown;
    juce::Label    stepLabel;
    NudgeInput     stepMult { juce::String::fromUTF8("\xc3\x97"), 1, 16, 1 };

    struct AssignmentRow : public juce::Component
    {
        DropdownSelect   destCombo;
        // shared depth + curve pair (was two raw juce::Sliders set up inline).
        BipolarSliderRow bipolarPair;
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
