#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Plugin/PluginProcessor.h"
#include "UI/GrooveGrid.h"
#include "UI/EnginePanel.h"
#include "Modulation/MuOnModDest.h"

#include "UI/ChannelHeaderBar.h"             // mu-core: shared per-layer header
#include "UI/ConfirmDialog.h"                // mu-core: shared confirm / name dialogs
#include "UI/ModulatorPanel.h"               // mu-core: shared modulator module
#include "UI/Components/LFOEditor.h"          // mu-core: drawable smooth-curve editor
#include "UI/Components/MuLookAndFeel.h"

#include <array>
#include <cmath>

namespace mu_on
{

// Main work area — the family per-voice layout (mirrors mu-tant's VoicePanel), top→bottom:
//   1. shared ChannelHeaderBar (lane name / reset / presets)  ← identical across the family
//   2. the selected lane's engine params (EnginePanel)         ← "voice editing params above"
//   3. the 909 step editor for the SELECTED lane (GrooveGrid)  ← single row, not the 4-lane grid
//   4. the shared modulation module (mu-core ModulatorPanel)   ← same as every other module
// setChannel() forwards the sidebar selection to all four and rebinds the modulator panel
// to that lane's VoiceSlot + destination provider. A 30 Hz timer drives the modulator playhead.
class GroovePanel : public juce::Component,
                    private juce::Timer
{
public:
    explicit GroovePanel(PluginProcessor& p)
        : proc(p), grid(p, p.pattern()), engine(p)
    {
        addAndMakeVisible(header);
        addAndMakeVisible(engine);
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
            bool e = false;
            while (! lock.compare_exchange_strong(e, true, std::memory_order_acquire)) e = false;
            proc.rumbleEnvelope().curvePoints = pts;
            lock.store(false, std::memory_order_release);
        };
        addChildComponent(rumbleEnvEditor);   // shown only for the Rumble lane

        for (int lane = 0; lane < kNumChannels; ++lane)
            modProviders[(size_t) lane] = makeModDestProvider(lane);

        // Fixed lanes: no rename / delete / add. Reset clears the lane's engine params +
        // modulators (mirrors mu-tant's per-voice reset); the preset list + Save are that
        // lane's track presets.
        header.setShowReset(true);
        header.setShowDelete(false);
        header.setNameEditable(false);
        header.onReset = [this]
        {
            mu_ui::confirmAsync(this, "Reset Track",
                                "Reset \"" + proc.getChannelName(currentChannel) + "\" to defaults?\nThis cannot be undone.",
                                "Reset", [this] { proc.resetTrack(currentChannel); setChannel(currentChannel); });
        };
        header.onPresetFileChosen = [this](const juce::File& f)
        {
            proc.loadTrackPreset(currentChannel, f);
            setChannel(currentChannel);   // re-read the grid row / envelope + modulators
            header.showPresetFile(f);
        };
        header.setSaveEnabled(proc.canSaveLayerPreset());   // demo: per-track save disabled
        header.onSave = [this]
        {
            if (! proc.canSaveLayerPreset()) return;
            juce::Component::SafePointer<GroovePanel> safe(this);
            mu_ui::promptTextAsync(this, "Save Track Preset", "Preset name:",
                                   proc.getChannelName(currentChannel), "Save",
                [safe](const juce::String& name)
                {
                    if (safe == nullptr || name.isEmpty()) return;
                    safe->proc.saveTrackPreset(safe->currentChannel, name);
                    safe->refreshPresetList();
                });
        };

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
        if (hasSteps) grid.setSelectedTrack(currentChannel);
        if (currentChannel == Rumble) rumbleEnvEditor.setPoints(proc.rumbleEnvelope().curvePoints);
        engine.setChannel(currentChannel);

        header.setLayerName(proc.getChannelName(currentChannel));
        refreshPresetList();   // each lane lists only its own track presets
        header.setColour(MuLookAndFeel::channelPalette[
            (size_t) (proc.getChannelColourIndex(currentChannel) % MuLookAndFeel::kChannelPaletteSize)]);

        modPanel.setVoiceSlot(&proc.voiceSlot(currentChannel));
        modPanel.setDestProvider(&modProviders[(size_t) currentChannel]);
        resized();   // grid show/hide changes the engine area — re-lay out
        repaint();
    }

    void resized() override
    {
        // Metal style: each area sits inside its own metal panel, so children are inset.
        const bool metal = MuLookAndFeel::isMetal(*this);
        const int  in    = metal ? mu_ui::s(MuLookAndFeel::kChannelInset) : 0;

        auto r = getLocalBounds();
        headerR = r.removeFromTop(mu_ui::s(ChannelHeaderBar::kHeight) + (metal ? mu_ui::s(4) : 0));
        header.setBounds(metal ? headerR.reduced(mu_ui::s(4), mu_ui::s(2)) : headerR);
        r.removeFromTop(mu_ui::s(4));

        // Shared modulation module at the bottom (same footprint as the other products).
        modR = r.removeFromBottom(juce::jmax(mu_ui::s(220), juce::roundToInt(r.getHeight() * 0.42f)));
        modPanel.setBounds(modR.reduced(in));
        r.removeFromTop(mu_ui::s(4));

        // The selected lane's step editor sits just under the engine params; for the Rumble
        // lane the drawable bar-volume envelope takes the same slot instead.
        {
            slotR = r.removeFromBottom(mu_ui::s(kGridH) + 2 * in);
            r.removeFromBottom(mu_ui::s(4));
            if (currentChannel == Rumble) rumbleEnvEditor.setBounds(slotR.reduced(in));
            else                          grid.setBounds(slotR.reduced(in));
        }

        // Engine params fill what's left, directly under the header.
        engineR = r;
        engine.setBounds(r.reduced(in));
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(MuLookAndFeel::colour(MuLookAndFeel::panelBackground));
        if (! MuLookAndFeel::isMetal(*this)) return;

        // Metal style: preset bar in the lane colour, every other area in the app colour.
        const auto laneCol = MuLookAndFeel::channelPalette[(size_t) juce::jlimit(0, MuLookAndFeel::kChannelPaletteSize - 1,
                                                                                  proc.getChannelColourIndex(currentChannel))];
        const auto accent  = MuLookAndFeel::appAccent(*this);
        MuLookAndFeel::drawAccentPanel(g, headerR.reduced(2).toFloat(), laneCol);
        for (auto rr : { engineR, slotR, modR })
            MuLookAndFeel::drawAccentPanel(g, rr.reduced(2).toFloat(), accent);
    }

    void lookAndFeelChanged() override { resized(); repaint(); }

    int getChannel() const noexcept { return currentChannel; }

    // Rescan this lane's track presets into the header's preset list.
    void refreshPresetList() { header.setPresetFiles(proc.trackPresetFiles(currentChannel)); }

private:
    void timerCallback() override
    {
        const double beat = proc.getInternalBeatPos();
        modPanel.setPlayheadBeat(beat);
        if (rumbleEnvEditor.isVisible())
            rumbleEnvEditor.setPlayheadPhase((float) (std::fmod(juce::jmax(0.0, beat), 4.0) / 4.0));
    }

    PluginProcessor& proc;
    int currentChannel = 0;

    ChannelHeaderBar header;
    EnginePanel      engine;
    GrooveGrid       grid;
    LFOEditor        rumbleEnvEditor;   // drawable bar-volume envelope (Rumble lane only)
    ModulatorPanel   modPanel;
    std::array<ModDestProvider, kNumChannels> modProviders;

    static constexpr int kGridH = GrooveGrid::kStepEditorHeight;
    juce::Rectangle<int> headerR, engineR, slotR, modR;   // panel areas (metal style)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GroovePanel)
};

} // namespace mu_on
