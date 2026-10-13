#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Plugin/PluginProcessor.h"
#include "UI/ParamKnobGrid.h"                // mu-core: spec-driven knob/combo grid
#include "UI/StepRows.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/LFOEditor.h"          // mu-core: drawable smooth-curve editor
#include "UI/Components/NoteLengthControl.h"  // mu-core: the Loop-style length row
#include "UI/Components/MuLookAndFeel.h"

#include <array>
#include <functional>
#include <memory>

namespace mu_on
{

// What fills the content band (design-ui-family §5) of the shared LayerPanel: the shown track's
// engine and its step rows. Two metal panels, the second the flex one:
//   1. ENGINE   an "Engine" dropdown (and, for a Rumble, its "Source") first, then the selected engine's
//               knobs. Switching engine asks nothing and keeps every engine's settings on the track.
//   2. GROOVE | STEPS   the global Swing / Accent (a Rumble track shows its volume envelope instead) and
//               one row of steps per track (StepRows).
// setLayer() rebinds it to the shown track; the engine can also change underneath it (host automation,
// a preset), which the timer notices.
class TrackContent : public juce::Component, private juce::Timer
{
public:
    explicit TrackContent(PluginProcessor& processor);
    ~TrackContent() override { stopTimer(); }

    // Show track `idx`: its engine's controls, its Rumble source and envelope, its row selected.
    void setLayer(int idx);

    // A click in a step row selected another track.
    std::function<void(int)> onRowClicked;
    // The shown track's engine changed (the user picked one, or it changed underneath): the editor
    // refreshes everything that depends on it (sidebar names, modulation destinations).
    std::function<void()> onEngineChanged;

    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override { resized(); repaint(); }

private:
    void timerCallback() override;
    void rebuild();                         // rebind every control to the shown track and engine
    void refreshSourceList();
    void refreshSourceSelection();
    void engineChosen(int id);
    void sourceChosen(int id);
    juce::Colour layerColour() const;

    PluginProcessor& proc;
    int        currentLayer = 0;
    EngineType shownEngine  = EngineType::Kick;
    juce::uint64 shownLayerSignature = 0;   // the live layers' engines, for the Source list

    juce::Label    engineCaption { {}, "Engine" }, sourceCaption { {}, "Source" };
    DropdownSelect engineSelect, sourceSelect;
    mu_ui::ParamKnobGrid grid;
    StepRows       rows;

    KnobWithLabel swingKnob { "Swing" }, accentKnob { "Accent" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> swingAtt, accentAtt;
    LFOEditor         rumbleEnvEditor;                // the Rumble track's drawable volume envelope
    NoteLengthControl rumbleLength { "Loop" };        // its length: the same control as a modulator's Loop

    juce::Rectangle<int> engineR, slotR;              // the two panels
    juce::Rectangle<int> engineBoxR, grooveBoxR, stepsBoxR;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackContent)
};

} // namespace mu_on
