#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Plugin/ProcessorBase.h"
#include "UI/Components/MuLookAndFeel.h"

namespace mu_on
{

// Placeholder main panel. The 909 groove grid + per-channel engine controls land in
// later increments; for the scaffold this marks the product area and tracks the
// selected channel so the shell visibly responds to sidebar selection.
class EnginePanel : public juce::Component
{
public:
    explicit EnginePanel(ProcessorBase& processor) : proc(processor) {}

    void setChannel(int idx) { currentChannel = idx; repaint(); }
    int  getChannel() const noexcept { return currentChannel; }

    void paint(juce::Graphics& g) override
    {
        using Id = MuLookAndFeel::ColourIds;
        auto bounds = getLocalBounds().toFloat().reduced(8.0f);

        g.fillAll(MuLookAndFeel::colour(Id::panelBackground));
        g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
        g.drawRoundedRectangle(bounds, 6.0f, 1.5f);

        const auto accent = MuLookAndFeel::channelPalette[
            (size_t) juce::jlimit(0, MuLookAndFeel::kChannelPaletteSize - 1,
                                  proc.getChannelColourIndex(currentChannel))];

        auto centre = getLocalBounds();
        g.setColour(accent);
        g.setFont(juce::Font(juce::FontOptions(32.0f, juce::Font::bold)));
        g.drawText(juce::String(juce::CharPointer_UTF8("\xce\xbc-On")),
                   centre.removeFromTop(getHeight() / 2).translated(0, 20),
                   juce::Justification::centredBottom, false);

        juce::String msg(juce::CharPointer_UTF8("Groove sequencer \xe2\x80\x94 grid + engines incoming"));
        const juce::String name = proc.getChannelName(currentChannel);
        if (name.isNotEmpty()) msg += "   (" + name + ")";
        g.setColour(MuLookAndFeel::colour(Id::mutedText));
        g.setFont(juce::Font(juce::FontOptions(15.0f)));
        g.drawText(msg, centre.removeFromTop(40), juce::Justification::centredTop, false);
    }

private:
    ProcessorBase& proc;
    int currentChannel = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EnginePanel)
};

} // namespace mu_on
