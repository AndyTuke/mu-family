#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Plugin/ProcessorBase.h"
#include "UI/Components/MuLookAndFeel.h"

namespace mu_toni
{

// Placeholder main panel for the (not-yet-defined) μ-Toni synth engine. The
// shared shell (transport, sidebar, mixer, settings) is fully wired around it;
// this panel just marks the product-specific area that the engine + its
// sequencer UI will fill. setLayer() tracks the sidebar selection so the
// scaffold visibly responds to layer changes.
class EnginePanel : public juce::Component
{
public:
    explicit EnginePanel(ProcessorBase& processor) : proc(processor) {}

    void setLayer(int idx) { currentLayer = idx; repaint(); }
    int  getLayer() const noexcept { return currentLayer; }

    void paint(juce::Graphics& g) override
    {
        using Id = MuLookAndFeel::ColourIds;
        auto bounds = getLocalBounds().toFloat().reduced(8.0f);

        g.fillAll(MuLookAndFeel::colour(Id::panelBackground));

        // "Area reserved" border so it reads as intentionally-blank, not broken.
        g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
        g.drawRoundedRectangle(bounds, 6.0f, 1.5f);

        const auto accent = MuLookAndFeel::channelPalette[
            (size_t) juce::jlimit(0, MuLookAndFeel::kChannelPaletteSize - 1,
                                  proc.getChannelColourIndex(currentLayer))];

        auto centre = getLocalBounds();
        g.setColour(accent);
        g.setFont(juce::Font(juce::FontOptions(32.0f, juce::Font::bold)));
        g.drawText(juce::String(juce::CharPointer_UTF8("\xce\xbc-Toni")),
                   centre.removeFromTop(getHeight() / 2).translated(0, 20),
                   juce::Justification::centredBottom, false);

        juce::String msg(juce::CharPointer_UTF8("Synth engine area \xe2\x80\x94 to be defined"));
        const juce::String layerName = proc.getChannelName(currentLayer);
        if (layerName.isNotEmpty()) msg += "   (" + layerName + ")";
        g.setColour(MuLookAndFeel::colour(Id::mutedText));
        g.setFont(juce::Font(juce::FontOptions(15.0f)));
        g.drawText(msg, centre.removeFromTop(40), juce::Justification::centredTop, false);
    }

private:
    ProcessorBase& proc;
    int currentLayer = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EnginePanel)
};

} // namespace mu_toni
