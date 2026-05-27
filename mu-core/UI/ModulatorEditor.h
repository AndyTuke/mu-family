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
#include "UI/Components/MuLookAndFeel.h"
#include "Sequencer/ControlSequence.h"
#include "Modulation/ModulationMatrix.h"
#include "Modulation/ModulationAssignment.h"

// Product-supplied modulation-destination provider. Each mu-family product
// (mu-clid, mu-tant, …) declares its own destination IDs + alias labels +
// dropdown layout. The provider's two callbacks let the mu-core ModulatorEditor
// / ModMatrixPanel populate the destination dropdown and resolve a selected
// 1-based dropdown ID back to the persisted destination string.
//
// driveChar is forwarded for products that switch destinations dynamically
// (mu-clid swaps the visible Insert algo slots). Products that don't need it
// ignore it.
struct ModDestProvider
{
    std::function<void(DropdownSelect& dd, int driveChar)> populate;
    std::function<std::string(int dropdownId)>             resolveId;
    // Reverse lookup — given a stored destination string, return the 1-based
    // dropdown ID currently representing it (or 0 if it isn't in the provider's
    // current destination set). Used to highlight the right item when rebuilding
    // the dropdown after a preset load.
    std::function<int(const std::string& destId)>          findDropdownId;
};

// Editor panel for one ControlSequence.
// Layout: mode/polarity header | LFO or Step editor | loop (+ step) timing | assignment list | add button
class ModulatorEditor : public juce::Component
{
public:
    ModulatorEditor();

    // modLock must be the VoiceSlot::modLock for the owning slot.
    // All mutations to cs/matrix are performed under this lock.
    void setData(ControlSequence* cs, ModulationMatrix* matrix, juce::Colour modColour, int index,
                 std::atomic<bool>* modLock = nullptr);

    // Product supplies its destination provider. Called by ModulatorPanel
    // forwarding from its own setDestProvider.
    void setDestProvider(const ModDestProvider* p);

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

    ControlSequence*       cs            = nullptr;
    ModulationMatrix*      matrix        = nullptr;
    std::atomic<bool>*     rhythmModLock = nullptr;
    const ModDestProvider* destProvider  = nullptr;
    juce::Colour           modColour;
    int                    modIndex      = 0;

    // Acquire VoiceSlot::modLock (spin). Call before any mutation of cs or matrix.
    void lockMod();
    // Release VoiceSlot::modLock.
    void unlockMod();

    DropdownSelect modeDropdown;    // Smooth / Stepped
    SegmentControl polarityCtrl{ {"Uni","Bi"} };
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

        AssignmentRow(const std::string& assignId, int driveChar, const ModDestProvider* provider);
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
    static constexpr int kEditorH = 150;  // timing controls live in the header row
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
