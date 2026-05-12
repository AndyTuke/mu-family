#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Components/DropdownSelect.h"
#include "Components/AddButton.h"
#include "Components/MuClidLookAndFeel.h"
#include "ModulatorEditor.h"
#include "../Sequencer/Rhythm.h"

// Overview of all modulation assignments across all ControlSequences in a rhythm.
class ModMatrixPanel : public juce::Component
{
public:
    ModMatrixPanel();

    void setRhythm(Rhythm* r);
    void setInsertAlgorithm(int driveChar);
    void refresh();

    std::function<void()> onChange;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    Rhythm* rhythm = nullptr;

    struct MatrixRow : public juce::Component
    {
        juce::Label      sourceLabel;
        DropdownSelect   destCombo;
        juce::Slider     depthSlider;
        juce::Slider     curveSlider;   // #224
        juce::TextButton removeBtn { "x" };
        std::string      assignId;

        std::function<void()>                         onRemove;
        std::function<void(const std::string& dest)>  onDestChange;
        std::function<void(float depth)>              onDepthChange;
        std::function<void(float curve)>              onCurveChange;   // #224

        MatrixRow(const ModulationAssignment& a, int csIndex, int driveChar);
        void resized() override;
    };

    std::vector<std::unique_ptr<MatrixRow>> matrixRows;
    AddButton addBtn { "Assignment" };
    int currentDriveChar = 0;

    static constexpr int kHeaderH = 20;
    static constexpr int kRowH    = 26;
    static constexpr int kAddBtnH = 28;

    void rebuildRows();
};
