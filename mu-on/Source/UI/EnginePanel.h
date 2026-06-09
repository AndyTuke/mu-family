#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Plugin/ProcessorBase.h"
#include "Plugin/MuOnChannels.h"
#include "UI/ParamKnobGrid.h"   // shared spec-driven knob/combo grid

// EnginePanel — the controls for the currently-selected instrument lane. On setChannel()
// it feeds that engine's {paramId,label} specs to the shared mu_ui::ParamKnobGrid, which
// builds + attaches + lays out the knobs/combos. This panel only adds the lane header.
namespace mu_on
{

class EnginePanel : public juce::Component
{
public:
    explicit EnginePanel(ProcessorBase& processor);

    void setChannel(int idx);
    int  getChannel() const noexcept { return currentChannel; }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    ProcessorBase&      proc;
    int                 currentChannel = 0;
    mu_ui::ParamKnobGrid grid;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EnginePanel)
};

} // namespace mu_on
