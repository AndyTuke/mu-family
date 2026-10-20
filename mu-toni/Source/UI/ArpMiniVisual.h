#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace mu_toni
{

// Per-layer mini-graphic for the shared LayerSidebar: a coloured disc with the layer number, or with a given text
// (a host whose sidebar item already names the slot by number, mu-Tronic, shows the type's initial instead, so the
// number is not drawn twice). A placeholder until a real arp visual is designed.
class ArpMiniVisual : public juce::Component
{
public:
    ArpMiniVisual(juce::Colour c, int index) : colour(c), text(juce::String(index + 1)) {}
    ArpMiniVisual(juce::Colour c, juce::String disc) : colour(c), text(std::move(disc)) {}

    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced(2.0f);
        g.setColour(colour.withAlpha(0.16f));
        g.fillEllipse(r);
        g.setColour(colour);
        g.drawEllipse(r, 1.5f);
        g.setFont(juce::Font(juce::FontOptions(r.getHeight() * 0.42f, juce::Font::bold)));
        g.drawText(text, getLocalBounds(), juce::Justification::centred, false);
    }

private:
    juce::Colour colour;
    juce::String text;
};

} // namespace mu_toni
