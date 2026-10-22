#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/TrackHost.h"                  // what the content reads from the layer's host
#include "Sequencer/Track.h"                // the layer: its row, envelope and modulation matrix
#include "Modulation/MuOnModDest.h"        // the engines' modulation target tables
#include "UI/ParamKnobGrid.h"                // mu-core: spec-driven knob/combo grid
#include "UI/StepRows.h"
#include "UI/TrackStepRow.h"
#include "UI/LayerContent.h"                // mu-core: the base every layer type's content derives from
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/LFOEditor.h"          // mu-core: drawable smooth-curve editor
#include "UI/Components/NoteLengthControl.h"  // mu-core: the Loop-style length row
#include "UI/Components/MuLookAndFeel.h"
#include "UI/Voice/InsertSubsection.h"   // mu-core: the shared per-channel insert panel

#include <array>
#include <functional>
#include <memory>

class ProcessorBase;

namespace mu_on
{

// What fills the content band (design-ui-family §5) of the shared LayerPanel: the shown track's
// engine and its step rows. Two metal panels, the second the flex one:
//   1. ENGINE | <NAME> VOICE | INSERT   the "Engine" dropdown (and, for a Rumble, its "Source") in a box of its
//               own before the voice, then the selected engine's knobs, then the track's insert effect.
//               Switching engine asks nothing and keeps every engine's settings on the track.
//   2. LAYER | STEP | STEPS   the track's own Swing / Accent, its three FX sends, its Probability and a Solo button
//               (a Rumble track shows its volume envelope instead); the selected step's probability, loop ratio
//               and sound; and one row of steps per track (StepRows).
// setLayer() rebinds it to the shown track; the engine can also change underneath it (host automation,
// a preset), which the timer notices.
//
// A track whose engine the host fixes (Track::engineLocked, a mu-Tronic layer) differs in two ways, both
// subtractions: no Engine selector (the knob grid moves left into the freed width, a Rumble's Source selector
// takes the first position), and the Steps box shows the one track's row (TrackStepRow) at the height
// StepRows gives each of its rows, not the list of every track's rows.
class TrackContent : public LayerContent, private juce::Timer
{
public:
    // `base` is for the shared mu-core sections that take the processor (this content uses none yet);
    // everything the content itself reads comes through `host`.
    TrackContent(ProcessorBase& base, TrackHost& host);
    ~TrackContent() override { stopTimer(); }

    // Show track `idx`: its engine's controls, its Rumble source and envelope, its row selected.
    void setLayer(int idx) override;

    // Let go of every knob's modulation binding; called before a track is removed or moved, because the
    // arcs hold the track's matrix. setLayer rebinds.
    void releaseModulationArcs() override { grid.clearModBindings(); insertSub.clearGRSource(); }

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
    void rebuildGrid();                     // the engine's knob grid alone (specs, arcs, tooltips)
    int  shownAlgorithm() const;            // the shown FM Perc track's Algorithm index
    void bindModulationArcs(std::vector<mu_ui::ParamKnobGrid::Spec>& specs) const;   // live arcs on the knobs the target table drives
    void refreshSourceList();
    void refreshSourceSelection();
    void engineChosen(int id);
    void sourceChosen(int id);
    juce::Colour layerColour() const;

    TrackHost& host;
    int        currentLayer = 0;
    EngineType shownEngine  = EngineType::Kick;
    int        gridAlgorithm = -1;          // the FM Perc algorithm the grid's tooltips describe
    juce::uint64 shownLayerSignature = 0;   // the live layers' engines, for the Source list

    juce::Label    engineCaption { {}, "Engine" }, sourceCaption { {}, "Source" };
    DropdownSelect engineSelect, sourceSelect;
    // The master key and scale (global root / scale, the family tonal row), under the Engine selector.
    juce::Label    rootCaption { {}, "Root" }, scaleCaption { {}, "Scale" };
    DropdownSelect rootSelect, scaleSelect;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> rootAtt, scaleAtt;
    mu_ui::ParamKnobGrid grid;
    StepRows       rows;                              // every track's row (a track the host does not lock)
    TrackStepRow   soloRow;                           // the shown track's one row (a locked track)
    bool           locked = false;                    // the shown track's engine is fixed by its host

    KnobWithLabel swingKnob { "Swing" }, accentKnob { "Accent" };
    KnobWithLabel sendEffKnob { "FX" }, sendDlyKnob { "Delay" }, sendRevKnob { "Reverb" };   // the strip's three sends
    KnobWithLabel probKnob { "Prob" };                       // the layer's Probability: the share of its notes that play
    // The STEP panel: the selected step's own probability (multiplied with the layer's), loop ratio (the Nth of every M
    // passes of the row) and, on a Hat, which sound it plays. Not parameters: they edit the track's step row.
    KnobWithLabel  stepProbKnob { "Prob" };
    juce::Label    loopCaption { {}, "Play" }, voiceCaption { {}, "Sound" }, octaveCaption { {}, "Octave" }, noteCaption { {}, "Note" };
    DropdownSelect loopSelect, voiceSelect, octaveSelect, noteSelect;   // Octave -2..+2 and Note -12..+12 (a Kick or Bass step's pitch)
    std::array<int, mu_limits::kMaxLayers> selectedStepOf {};   // each layer's selected step (right-click), shown in the panel
    void refreshStepPanel();                                    // the controls follow the shown layer's selected step
    void selectStep(int step);
    void layoutStepPanel();
    juce::TextButton soloBtn { "Solo" };                                                      // the strip's solo
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> swingAtt, accentAtt, sendEffAtt, sendDlyAtt, sendRevAtt, probAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloAtt;
    InsertSubsection insertSub;                       // the track's insert effect (t{N}_drvChar, t{N}_insP1..4)
    LFOEditor         rumbleEnvEditor;                // the Rumble track's drawable volume envelope
    NoteLengthControl rumbleLength { "Loop" };        // its length: the same control as a modulator's Loop

    juce::Rectangle<int> engineR, slotR;              // the two panels
    juce::Rectangle<int> engineBoxR, voiceBoxR, insertBoxR, grooveBoxR, stepBoxR, stepsBoxR;   // engineBoxR: the Engine / Source selectors

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackContent)
};

} // namespace mu_on
