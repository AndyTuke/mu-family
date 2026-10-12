#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace mu_on
{

// Per-track mini-graphic for the shared LayerSidebar: a coloured disc with the instrument's
// initial (Kick / Bass / Hat / Snare / Rumble). Replaced by a richer engine / step glyph later.
class TrackMiniVisual : public juce::Component
{
public:
    TrackMiniVisual(juce::Colour c, juce::String initial) : colour(c), label(std::move(initial)) {}

    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced(2.0f);
        g.setColour(colour.withAlpha(0.16f));
        g.fillEllipse(r);
        g.setColour(colour);
        g.drawEllipse(r, 1.5f);
        g.setFont(juce::Font(juce::FontOptions(r.getHeight() * 0.42f, juce::Font::bold)));
        g.drawText(label, getLocalBounds(), juce::Justification::centred, false);
    }

private:
    juce::Colour colour;
    juce::String label;
};

} // namespace mu_on
