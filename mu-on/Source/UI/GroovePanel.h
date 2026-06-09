#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Plugin/PluginProcessor.h"
#include "UI/GrooveGrid.h"
#include "UI/EnginePanel.h"
#include "Modulation/MuOnModDest.h"

#include "UI/ChannelHeaderBar.h"             // mu-core: shared per-layer header
#include "UI/ModulatorPanel.h"               // mu-core: shared modulator module
#include "UI/Components/LFOEditor.h"          // mu-core: drawable smooth-curve editor
#include "Modulation/ModulatorSerialise.h"   // mu-core: clearModulators
#include "UI/Components/MuLookAndFeel.h"

#include <array>
#include <cmath>

namespace mu_on
{

// Main work area — the family per-voice layout (mirrors mu-tant's VoicePanel), top→bottom:
//   1. shared ChannelHeaderBar (lane name / reset)            ← identical across the family
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

        // Fixed lanes: no rename / delete / add. Reset clears the lane's engine params
        // + modulators (mirrors mu-tant's per-voice reset); Save is deferred with preset I/O.
        header.setShowReset(true);
        header.onReset = [this] { resetCurrentLane(); };

        startTimerHz(30);   // modulator playhead
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
        header.setColour(MuLookAndFeel::channelPalette[
            (size_t) (proc.getChannelColourIndex(currentChannel) % MuLookAndFeel::kChannelPaletteSize)]);

        modPanel.setVoiceSlot(&proc.voiceSlot(currentChannel));
        modPanel.setDestProvider(&modProviders[(size_t) currentChannel]);
        resized();   // grid show/hide changes the engine area — re-lay out
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        header.setBounds(r.removeFromTop(mu_ui::s(ChannelHeaderBar::kHeight)));
        r.removeFromTop(mu_ui::s(4));

        // Shared modulation module at the bottom (same footprint as the other products).
        modPanel.setBounds(r.removeFromBottom(juce::jmax(mu_ui::s(220), juce::roundToInt(r.getHeight() * 0.42f))));
        r.removeFromTop(mu_ui::s(4));

        // The selected lane's step editor sits just under the engine params; for the Rumble
        // lane the drawable bar-volume envelope takes the same slot instead.
        {
            auto slot = r.removeFromBottom(mu_ui::s(kGridH));
            r.removeFromBottom(mu_ui::s(4));
            if (currentChannel == Rumble) rumbleEnvEditor.setBounds(slot);
            else                          grid.setBounds(slot);
        }

        // Engine params fill what's left, directly under the header.
        engine.setBounds(r);
    }

private:
    void timerCallback() override
    {
        const double beat = proc.getInternalBeatPos();
        modPanel.setPlayheadBeat(beat);
        if (rumbleEnvEditor.isVisible())
            rumbleEnvEditor.setPlayheadPhase((float) (std::fmod(juce::jmax(0.0, beat), 4.0) / 4.0));
    }

    // Reset the selected lane: engine params → defaults, then clear its modulators and
    // rebind the panel so the cleared state shows.
    void resetCurrentLane()
    {
        static const char* kPrefix[kNumChannels] = { "k_", "b_", "h_", "s_", "r_" };
        const juce::String prefix = kPrefix[(size_t) currentChannel];
        for (auto* prm : proc.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(prm))
                if (rp->getParameterID().startsWith(prefix))
                    rp->setValueNotifyingHost(rp->getDefaultValue());

        mu_pp::clearModulators(proc.voiceSlot(currentChannel));
        modPanel.setVoiceSlot(&proc.voiceSlot(currentChannel));   // refresh the now-empty slot
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GroovePanel)
};

} // namespace mu_on
