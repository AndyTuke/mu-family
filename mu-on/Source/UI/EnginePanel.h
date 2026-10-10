#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Plugin/ProcessorBase.h"
#include "Plugin/MuOnChannels.h"
#include "UI/ParamKnobGrid.h"   // shared spec-driven knob/combo grid

// EnginePanel — the controls for one instrument lane (fixed at construction): that engine's
// {paramId,label} specs go to the shared mu_ui::ParamKnobGrid, which builds + attaches + lays
// out the knobs/combos. The editor shows one per lane, all at once. This panel only adds the
// lane header (flat style).
namespace mu_on
{

class EnginePanel : public juce::Component
{
public:
    EnginePanel(ProcessorBase& processor, int lane);

    int  getChannel() const noexcept { return currentChannel; }
    int  getPreferredWidth() const { return grid.getPreferredWidth(); }   // one row of this lane's controls

    void paint(juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override { resized(); repaint(); }

private:
    ProcessorBase&      proc;
    const int           currentChannel;
    mu_ui::ParamKnobGrid grid;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EnginePanel)
};

} // namespace mu_on
