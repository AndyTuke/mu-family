#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Plugin/PluginProcessor.h"
#include "UI/GrooveGrid.h"
#include "UI/EnginePanel.h"

namespace mu_on
{

// Main work area: the 909 GrooveGrid on top, the per-channel EnginePanel beneath.
// setChannel() forwards the sidebar selection to both (grid highlights the row, the
// engine panel shows that instrument's controls). The engine panel is the increment-1
// placeholder for now; its knobs land with the engines.
class GroovePanel : public juce::Component
{
public:
    explicit GroovePanel(PluginProcessor& p)
        : grid(p, p.pattern()), engine(p)
    {
        addAndMakeVisible(grid);
        addAndMakeVisible(engine);
    }

    void setChannel(int idx) { grid.setSelectedTrack(idx); engine.setChannel(idx); }

    void resized() override
    {
        auto r = getLocalBounds();
        grid.setBounds(r.removeFromTop(juce::roundToInt(r.getHeight() * 0.60f)));
        engine.setBounds(r);
    }

private:
    GrooveGrid  grid;
    EnginePanel engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GroovePanel)
};

} // namespace mu_on
