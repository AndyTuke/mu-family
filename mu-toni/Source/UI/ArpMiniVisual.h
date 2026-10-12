#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace mu_toni
{

// Per-layer mini-graphic for the shared LayerSidebar: a coloured disc with the layer number.
// A placeholder until a real arp visual is designed.
class ArpMiniVisual : public juce::Component
{
public:
    ArpMiniVisual(juce::Colour c, int index) : colour(c), idx(index) {}

    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced(2.0f);
        g.setColour(colour.withAlpha(0.16f));
        g.fillEllipse(r);
        g.setColour(colour);
        g.drawEllipse(r, 1.5f);
        g.setFont(juce::Font(juce::FontOptions(r.getHeight() * 0.42f, juce::Font::bold)));
        g.drawText(juce::String(idx + 1), getLocalBounds(), juce::Justification::centred, false);
    }

private:
    juce::Colour colour;
    int idx;
};

} // namespace mu_toni
