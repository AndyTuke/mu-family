#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace mu_ui
{

// A layer's sidebar picture and chooser-card picture: a disc in the layer's colour with a short text in it (the
// layer number, or the type's initial where the sidebar item already engraves the number). One drawing for every
// product and type, drawn to whatever size it is given; a type with a richer picture later replaces its factory.
class LayerDiscVisual : public juce::Component
{
public:
    LayerDiscVisual(juce::Colour discColour, juce::String discText) : colour(discColour), text(std::move(discText)) {}

    void setColour(juce::Colour c) { if (c != colour) { colour = c; repaint(); } }

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

} // namespace mu_ui
