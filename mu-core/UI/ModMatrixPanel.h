#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/AddButton.h"
#include "UI/Components/BipolarSliderRow.h"
#include "UI/Components/MuLookAndFeel.h"
#include "ModulatorEditor.h"
#include "Sequencer/VoiceSlot.h"

// Overview of all modulation assignments across all ControlSequences in a voice slot.
class ModMatrixPanel : public juce::Component
{
public:
    ModMatrixPanel();

    void setVoiceSlot(VoiceSlot* slot);
    void setInsertAlgorithm(int driveChar);
    void setDestProvider(const ModDestProvider* p);
    void refresh();

    std::function<void()> onChange;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    VoiceSlot*             voiceSlot    = nullptr;
    const ModDestProvider* destProvider = nullptr;

    struct MatrixRow : public juce::Component
    {
        juce::Label      sourceLabel;
        DropdownSelect   destCombo;
        // shared depth + curve pair (was two raw juce::Sliders set up inline).
        BipolarSliderRow bipolarPair;
        juce::TextButton removeBtn { "x" };
        std::string      assignId;

        std::function<void()>                         onRemove;
        std::function<void(const std::string& dest)>  onDestChange;
        std::function<void(float depth)>              onDepthChange;
        std::function<void(float curve)>              onCurveChange;

        MatrixRow(const ModulationAssignment& a, int csIndex, int driveChar,
                  const ModDestProvider* provider);
        void resized() override;
    };

    std::vector<std::unique_ptr<MatrixRow>> matrixRows;
    AddButton        addBtn { "Assignment" };
    juce::TextButton matPrevBtn { juce::String::charToString(0x25B2) }; // ▲
    juce::TextButton matNextBtn { juce::String::charToString(0x25BC) }; // ▼
    juce::Label      matPageLabel;
    int              matPage = 0;
    int              currentDriveChar = 0;

    static constexpr int kHeaderH = 20;
    static constexpr int kRowH    = 26;
    static constexpr int kAddBtnH = 28;
    static constexpr int kPagerH  = 20;

    int  rowsPerPage() const;
    void updateMatPager();
    void rebuildRows();
};
