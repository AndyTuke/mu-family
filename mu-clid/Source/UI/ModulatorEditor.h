#pragma once
#include <atomic>
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/SegmentControl.h"
#include "UI/Components/NudgeInput.h"
#include "UI/Components/LFOEditor.h"
#include "UI/Components/StepEditor.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/AddButton.h"
#include "UI/Components/BipolarSliderRow.h"
#include "UI/Components/MuClidLookAndFeel.h"
#include "Sequencer/ControlSequence.h"
#include "Modulation/ModulationMatrix.h"
#include "Modulation/ModulationAssignment.h"
#include "Modulation/ModulationDestinations.h"   // kTable now lives here
#include "Audio/InsertSlotConfig.h"               // kInsertAlgoSlots — per-algo slot labels
#include "Audio/AlgorithmNames.h"                 // kInsertAlgorithmNames — algo section heading

namespace ModDest
{
    // kTable + Dest struct moved to Source/Modulation/ModulationDestinations.h
    // so non-UI code (preset deserialiser) can validate assignments against the
    // same single source of truth. UI populate() helper stays here.

    // Populate dd with destinations grouped by section, showing only insert destinations
    // relevant for driveChar (0=None,1=SoftClip,2=HardClip,3=Fold,4=Bitcrusher,5=Clipper,
    // 6=EQ,7=Comp,8=Lim,9=RingMod,10=TapeSat).
    // Uses stable 1-based indices (= table index + 1) so saved assignments survive changes.
    // populate() depends on insert.p1..p4 occupying kTable indices 10..13 so it can
    // compute IDs as `10 + slot + 1`. A runtime invariant test in InsertAlgoTableTests
    // (#617 follow-up) fails fast at unit-test startup if anyone reorders the table.

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
        dd.addSectionHeading("Pitch");
        item(20);  // Pitch Octave (±3 oct)
        item(9);   // Pitch Semitones (±12 st)
        item(24);  // Pitch Env Depth (#223)

        // ── Filter ────────────────────────────────────────────────────────────
        dd.addSectionHeading("Filter");
        item(4);  item(5);   // Cutoff, Resonance
        item(6);  item(7);  item(8);  // Env Attack, Decay, Depth
        item(44);  // Low Cut (T5 follow-up)

        // ── Amp ───────────────────────────────────────────────────────────────
        dd.addSectionHeading("Amp");
        item(25);  // Amp Level (#223)
        item(0);  item(1);  item(2);  // Attack, Decay, Sustain
        // Amp Release (idx 3) is intentionally NOT a modulation target: a one-shot
        // step trigger never note-offs during playback, so the amp env never enters
        // its release phase — modulating the release time does nothing audible. The
        // release knob still works (it shapes a retired voice's hot-swap tail-out).
        item(26);  // Accent (#223)

        // ── Insert ────────────────────────────────────────────────────────────
        // Post-Stage-36: the 4 insert.p1..p4 destinations cover every algorithm; the
        // visible slots + their per-algo labels come from mu_ui::kInsertAlgoSlots so
        // the dropdown always reflects the active algorithm. Items added here keep the
        // SAME 1-based table ID (11..14 = kTable indices 10..13 = insert.p1..p4) so
        // saved assignments persist across algorithm changes — the dropdown text just
        // re-labels them to whatever the new algo names that slot. Hidden slots
        // (label == nullptr) are skipped so the menu reads cleanly per algo (#617).
        if (driveChar > 0 && driveChar < (int) std::size(mu_audio::kInsertAlgorithmNames) - 1
            && driveChar < 14)
        {
            const auto& slots = mu_ui::kInsertAlgoSlots[driveChar];
            // Only add the section heading if at least one slot is visible — keeps
            // the menu clean for algos with no insert mod destinations (None, etc.).
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
                // Text = the algo-specific slot label ("Drive", "Note", "Threshold", etc.).
                dd.addItem(slots[slot].label, 10 + slot + 1);
            }
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
    // Dice handler — randomise current modulator's values, leave structure.
    void randomiseValues();

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
    // Randomises the active modulator's values without touching its shape
    // (mode / polarity / loop+step timing / point or step count). Stepped
    // mode → fresh value per `stepValues[]` entry; Smooth mode → fresh y
    // per `curvePoints[]` entry (x positions and bezier handles preserved).
    juce::TextButton diceBtn { juce::String::fromUTF8("\xe2\x9a\x80") };  // ⚀

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
