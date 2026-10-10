#pragma once

#include "Audio/SpinLock.h"   // mu-core: spin lock helpers
#include <juce_gui_basics/juce_gui_basics.h>
#include "Plugin/PluginProcessor.h"
#include "UI/GrooveGrid.h"
#include "UI/EnginePanel.h"
#include "Modulation/MuOnModDest.h"

#include "UI/ChannelHeaderBar.h"             // mu-core: shared per-layer header
#include "UI/ConfirmDialog.h"                // mu-core: shared confirm / name dialogs
#include "UI/ModulatorPanel.h"               // mu-core: shared modulator module
#include "UI/Components/LFOEditor.h"          // mu-core: drawable smooth-curve editor
#include "UI/Components/NoteLengthControl.h"  // mu-core: the Loop-style length row
#include "UI/Components/MuLookAndFeel.h"

#include <array>
#include <cmath>
#include "UI/LayerPresetHeader.h"   // mu-core: shared per-slot preset header wiring

namespace mu_on
{

// Main work area — the family per-voice layout (mirrors mu-tant's VoicePanel), top→bottom:
//   1. shared ChannelHeaderBar (lane name / reset / presets)  ← identical across the family
//   2. every lane's engine params at once (one EnginePanel box each, flowed over as many rows as fit)
//   3. the 909 step editor for the SELECTED lane (GrooveGrid)  ← single row, not the 4-lane grid
//   4. the shared modulation module (mu-core ModulatorPanel)   ← same as every other module
// setChannel() forwards the sidebar selection to the step editor and rebinds the modulator panel
// to that lane's Layer + destination provider; the selected lane's engine box is outlined. A 30 Hz timer drives the modulator playhead.
class GroovePanel : public juce::Component,
                    private juce::Timer
{
public:
    explicit GroovePanel(PluginProcessor& p)
        : proc(p), grid(p, p.pattern())
    {
        addAndMakeVisible(header);
        for (int lane = 0; lane < kNumChannels; ++lane)
        {
            engines[(size_t) lane] = std::make_unique<EnginePanel>(p, lane);
            addAndMakeVisible(*engines[(size_t) lane]);
        }
        addAndMakeVisible(grid);
        addAndMakeVisible(modPanel);

        // Rumble's drawable bar-volume envelope sits in the step-grid slot (Rumble has no
        // steps). Drawn with the shared smooth-curve editor; edits write back to the
        // processor's envelope under its lock.
        rumbleEnvEditor.setUnipolar(true);
        rumbleEnvEditor.setPoints(proc.rumbleEnvelope().curvePoints);
        rumbleEnvEditor.onChange = [this](const std::vector<ControlSequence::CurvePoint>& pts)
        {
            auto& lock = proc.rumbleEnvLockRef();
            mu_core::spinLock(lock);
            proc.rumbleEnvelope().curvePoints = pts;
            mu_core::spinUnlock(lock);
        };
        addChildComponent(rumbleEnvEditor);   // shown only for the Rumble lane

        // The envelope's length — the same control as a modulator's Loop.
        rumbleLength.onChange = [this](NoteValue nv, NoteMod mod, int mult)
        {
            auto& lock = proc.rumbleEnvLockRef();
            mu_core::spinLock(lock);
            auto& env = proc.rumbleEnvelope();
            env.loopNoteValue  = nv;
            env.loopNoteMod    = mod;
            env.loopMultiplier = mult;
            mu_core::spinUnlock(lock);
        };
        addChildComponent(rumbleLength);

        for (int lane = 0; lane < kNumChannels; ++lane)
            modProviders[(size_t) lane] = makeModDestProvider(lane);

        // Fixed lanes: no rename / delete / add. Reset clears the lane's engine params +
        // modulators (mirrors mu-tant's per-voice reset); the preset list + Save are that
        // lane's track presets.
        header.setShowReset(true);
        header.setShowDelete(false);
        header.setNameEditable(false);
        // Reset / preset load / save: the shared per-slot wiring (mu-core LayerPresetHeader).
        mu_ui::wireSlotPresetHeader(header, *this, proc,
            { "Track",
              [this] { return currentChannel; },
              [this] { setChannel(currentChannel); },   // re-read the grid row / envelope + modulators
              {} });

        startTimerHz(mu_ui::kUiRefreshHz);   // modulator playhead
        setChannel(0);
    }

    ~GroovePanel() override { stopTimer(); }

    void setChannel(int idx)
    {
        currentChannel = juce::jlimit(0, kNumChannels - 1, idx);
        // Rumble is a processor lane with no step row — its drawable bar envelope takes the
        // grid slot instead.
        const bool hasSteps = currentChannel < kNumStepLanes;
        grid.setVisible(hasSteps);
        rumbleEnvEditor.setVisible(currentChannel == Rumble);
        rumbleLength.setVisible(currentChannel == Rumble);
        if (hasSteps) grid.setSelectedTrack(currentChannel);
        if (currentChannel == Rumble) showRumbleEnvelope();

        header.setLayerName(proc.getChannelName(currentChannel));
        refreshPresetList();   // each lane lists only its own track presets
        header.setColour(MuLookAndFeel::channelPalette[
            (size_t) (proc.getChannelColourIndex(currentChannel) % MuLookAndFeel::kChannelPaletteSize)]);

        modPanel.setVoiceSlot(&proc.voiceSlot(currentChannel));
        modPanel.setDestProvider(&modProviders[(size_t) currentChannel]);
        resized();   // grid show/hide changes the engine area — re-lay out
        repaint();
    }

    void resized() override { layoutMetal(); }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(MuLookAndFeel::colour(MuLookAndFeel::panelBackground));
        if (! MuLookAndFeel::isMetal(*this)) return;

        // Metal style: every panel painted in the app colour (the header bar lights its displays
        // in the lane colour), each section in a raised box named on a plate above it.
        const auto accent = MuLookAndFeel::appAccent(*this);
        for (auto rr : { headerR, engineR, slotR, modR })
            MuLookAndFeel::drawAccentPanel(g, rr.reduced(2).toFloat(), accent);
        for (int l = 0; l < kNumChannels; ++l)
            MuLookAndFeel::drawSections(g, *this, { { engineBoxes[(size_t) l], proc.getChannelName(l).toUpperCase() } }, accent);

        // The selected lane (the one whose steps are below) is outlined in its colour.
        g.setColour(MuLookAndFeel::channelPalette[(size_t) (proc.getChannelColourIndex(currentChannel) % MuLookAndFeel::kChannelPaletteSize)]);
        g.drawRoundedRectangle(engineBoxes[(size_t) currentChannel].toFloat().expanded(1.0f), 4.0f, 1.5f);

        const auto lane = proc.getChannelName(currentChannel).toUpperCase();
        if (currentChannel == Rumble)
            MuLookAndFeel::drawSections(g, *this, { { stepsBoxR, lane + " ENVELOPE" } }, accent);
        else
            MuLookAndFeel::drawSections(g, *this, { { grooveBoxR, "GROOVE" }, { stepsBoxR, lane + " STEPS" } }, accent);
    }

    // A click on a lane's engine box (its plate or the gaps between knobs) selects that lane.
    void mouseDown(const juce::MouseEvent& e) override
    {
        for (int l = 0; l < kNumChannels; ++l)
            if (engineBoxes[(size_t) l].expanded(0, mu_ui::s(MuLookAndFeel::kSectionPlateH)).contains(e.getPosition()))
            {
                if (onLaneClicked) onLaneClicked(l);
                return;
            }
    }

    std::function<void(int)> onLaneClicked;   // the editor points the sidebar at the clicked lane

    void paintOverChildren(juce::Graphics& g) override
    {
        if (! MuLookAndFeel::hasScrews(*this)) return;
        for (auto rr : { engineR, slotR, modR })
            MuLookAndFeel::drawPanelScrews(g, rr.reduced(2).toFloat());
        MuLookAndFeel::drawStripScrews(g, headerR.reduced(2).toFloat());   // the thin preset strip
    }

    void lookAndFeelChanged() override { resized(); repaint(); }

    int getChannel() const noexcept { return currentChannel; }

    // Rescan this lane's track presets into the header's preset list.
    void refreshPresetList() { mu_ui::refreshSlotPresetList(header, proc, currentChannel); }

private:
    // Metal style, as mu-Clid: panels edge to edge — preset strip, engine, steps, modulators —
    // content inset clear of the panels' corner screws, each section a raised box with its name
    // plate in the band above it. The engine box holds one row of the lane's controls (the
    // widest lane fits one row); the steps panel holds Groove (Swing / Accent) + the lane's
    // steps, or for Rumble its envelope, in a box of the same size so the layout never jumps.
    void layoutMetal()
    {
        using LF = MuLookAndFeel;
        using mu_ui::s;
        const int w = getWidth(), h = getHeight();
        const int padX   = s(LF::kScrewedPanelInset);   // content in from a panel's sides
        const int padY   = s(LF::kChannelInset);        //   … and from its top / bottom
        const int plateH = s(LF::kSectionPlateH);       // name plate band above each box
        const int clear  = s(LF::kSubPanelScrewClear);  // content in from a box's sides

        headerR = { 0, 0, w, s(ChannelHeaderBar::kHeight) + s(4) };
        header.setBounds(headerR.reduced(LF::hasScrews(*this) ? padX : s(4), s(2)));

        // Every lane's engine box, flowed left to right in sidebar order and wrapped to the next
        // row when the next box would overrun the panel; each is as wide as its controls need.
        const int engineBoxH = s(mu_ui::ParamKnobGrid::kCellH + 2 * LF::kSpaceS);
        const int gap        = s(LF::kVoiceDivW);
        const int rowH       = plateH + engineBoxH;
        int x = padX, row = 0;
        for (int l = 0; l < kNumChannels; ++l)
        {
            const int bw = engines[(size_t) l]->getPreferredWidth() + 2 * clear;
            if (x > padX && x + bw > w - padX) { x = padX; ++row; }
            engineBoxes[(size_t) l] = { x, headerR.getBottom() + padY + row * (rowH + padY) + plateH, bw, engineBoxH };
            engines[(size_t) l]->setBounds(engineBoxes[(size_t) l].reduced(clear, s(LF::kSpaceS)));
            x += bw + gap;
        }
        engineR = { 0, headerR.getBottom(), w, padY + (row + 1) * (rowH + padY) };

        const int boxH = s(GrooveGrid::kBoxH);
        slotR = { 0, engineR.getBottom(), w, padY + plateH + boxH + padY };
        const juce::Rectangle<int> slotBoxes(padX, slotR.getY() + padY + plateH, w - 2 * padX, boxH);
        if (currentChannel == Rumble)
        {
            grooveBoxR = {};
            stepsBoxR  = slotBoxes;
            layoutRumble(stepsBoxR.reduced(clear, s(LF::kSpaceS)));
        }
        else
        {
            grooveBoxR = slotBoxes.withWidth(s(GrooveGrid::kGrooveBoxW));
            stepsBoxR  = slotBoxes.withTrimmedLeft(grooveBoxR.getWidth() + s(LF::kVoiceDivW));
            grid.setBounds(slotBoxes);
        }

        modR = { 0, slotR.getBottom(), w, juce::jmax(s(220), h - slotR.getBottom()) };
        modPanel.setBounds(modR.reduced(padX, padY));
    }

    void timerCallback() override
    {
        const double beat = proc.getInternalBeatPos();
        modPanel.setPlayheadBeat(beat);
        header.setStagingBadge(proc.hasPendingSwap(currentChannel));   // "SWP" while a track preset waits for the wrap
        if (rumbleEnvEditor.isVisible())
        {
            const double len = juce::jmax(1.0e-6, proc.rumbleEnvelope().getLoopLengthBeats());
            rumbleEnvEditor.setPlayheadPhase((float) (std::fmod(juce::jmax(0.0, beat), len) / len));
        }
    }

    // The Rumble box: the Loop (length) row at the top left, the envelope beside it at full height.
    void layoutRumble(juce::Rectangle<int> r)
    {
        using mu_ui::s;
        rumbleLength.setMetalStyle(MuLookAndFeel::isMetal(*this));
        auto lengthColumn = r.removeFromLeft(s(NoteLengthControl::kWidth));
        rumbleLength.setBounds(lengthColumn.removeFromTop(s(NoteLengthControl::kHeight)));
        r.removeFromLeft(s(MuLookAndFeel::kSpaceS));
        rumbleEnvEditor.setBounds(r);
    }

    // Load the processor's envelope (points + length) into the editor and the Length row.
    void showRumbleEnvelope()
    {
        const auto& env = proc.rumbleEnvelope();
        rumbleEnvEditor.setPoints(env.curvePoints);
        rumbleLength.setLength(env.loopNoteValue, env.loopNoteMod, env.loopMultiplier);
    }

    PluginProcessor& proc;
    int currentChannel = 0;

    ChannelHeaderBar header;
    std::array<std::unique_ptr<EnginePanel>, kNumChannels> engines;   // one box per lane, all visible
    GrooveGrid       grid;
    LFOEditor        rumbleEnvEditor;   // drawable volume envelope (Rumble lane only)
    NoteLengthControl rumbleLength { "Loop" };     // its length — the same control as a modulator's Loop
    ModulatorPanel   modPanel;
    std::array<ModDestProvider, kNumChannels> modProviders;

    static constexpr int kGridH = GrooveGrid::kStepEditorHeight;
    juce::Rectangle<int> headerR, engineR, slotR, modR;   // panel areas (metal style)
    std::array<juce::Rectangle<int>, kNumChannels> engineBoxes;   // each lane's raised engine box
    juce::Rectangle<int> grooveBoxR, stepsBoxR;                   // the selected lane's Groove + steps boxes

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GroovePanel)
};

} // namespace mu_on
