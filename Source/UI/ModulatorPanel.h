#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Components/SegmentControl.h"
#include "Components/MuClidLookAndFeel.h"
#include "ModulatorEditor.h"
#include "ModMatrixPanel.h"
#include "../Sequencer/Rhythm.h"
#include "../MuLimits.h"

// Container panel: tab bar (Mod A–H + Matrix) + content area.
// Owns 8 ModulatorEditor instances (one per ControlSequence) and a ModMatrixPanel.
class ModulatorPanel : public juce::Component
{
public:
    ModulatorPanel();

    void setRhythm(Rhythm* r);
    void setInsertAlgorithm(int driveChar);

    // Drive the modulator playhead from the current song beat position.
    void setPlayheadBeat(double beat);

    std::function<void()> onChange;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    Rhythm* rhythm = nullptr;

    SegmentControl tabBar { {"A","B","C","D","E","F","G","H","Matrix"},
                            SegmentControl::ActiveStyle::General,
                            SegmentControl::DrawStyle::Bar };

    // Must equal Rhythm::MaxControlSequences — one tab per ControlSequence.
    static constexpr int kNumMods = mu_limits::kMaxControlSequences;
    ModulatorEditor  editors[kNumMods];
    ModMatrixPanel   matrixPanel;

    int activeTab = 0;

    static constexpr int kTabH = 28;

    static juce::Colour modColour(int index) noexcept;
    void showTab(int idx);
};
