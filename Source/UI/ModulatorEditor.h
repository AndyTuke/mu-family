#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Components/SegmentControl.h"
#include "Components/TimeSelector.h"
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
namespace ModDest
{
    inline const juce::StringArray ids {
        "amp.attack",    "amp.decay",    "amp.sustain",   "amp.release",
        "filter.cutoff", "filter.resonance",
        "fenv.attack",   "fenv.decay",   "fenv.depth",
        "euclid.a.hits", "euclid.a.rotate",
        "euclid.b.hits", "euclid.b.rotate"
    };
    inline const juce::StringArray labels {
        "Amp Attack",       "Amp Decay",       "Amp Sustain",      "Amp Release",
        "Filter Cutoff",    "Filter Resonance",
        "Filter Env Attack","Filter Env Decay", "Filter Env Depth",
        "Euclid A Hits",    "Euclid A Rotate",
        "Euclid B Hits",    "Euclid B Rotate"
    };
}

// Editor panel for one ControlSequence.
// Layout: mode/polarity header | LFO or Step editor | loop (+ step) timing | assignment list | add button
class ModulatorEditor : public juce::Component
{
public:
    ModulatorEditor();

    void setData(ControlSequence* cs, ModulationMatrix* matrix, juce::Colour modColour);

    std::function<void()> onChange;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    ControlSequence*  cs     = nullptr;
    ModulationMatrix* matrix = nullptr;
    juce::Colour      modColour;

    SegmentControl modeCtrl     { {"Smooth","Stepped"} };
    SegmentControl polarityCtrl { {"Bipolar","Unipolar"} };
    LFOEditor      lfoEditor;
    StepEditor     stepEditor;
    TimeSelector   loopNoteSelector;
    NudgeInput     loopMult { "Loop", 1, 16, 4 };
    TimeSelector   stepNoteSelector;
    NudgeInput     stepMult { "Step", 1, 16, 1 };

    struct AssignmentRow : public juce::Component
    {
        DropdownSelect   destCombo;
        juce::Slider     depthSlider;
        juce::TextButton removeBtn { "x" };
        std::string      id;

        std::function<void()>                         onRemove;
        std::function<void(const std::string& dest)>  onDestChange;
        std::function<void(float depth)>              onDepthChange;

        explicit AssignmentRow(const std::string& assignId);
        void resized() override;
    };

    std::vector<std::unique_ptr<AssignmentRow>> rows;
    AddButton addBtn { "Target" };

    static constexpr int kHeaderH = 28;
    static constexpr int kEditorH = 140;
    static constexpr int kTimingH = 28;
    static constexpr int kRowH    = 26;
    static constexpr int kAddBtnH = 28;

    void wireHeader();
    void wireTiming();
    void rebuildRows();
    void addTarget();
    void loadFromCS();
    void syncStepValues();
};
