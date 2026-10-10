#include "StatusLamp.h"
#include "MuLookAndFeel.h"

void StatusLamp::setLabel(const juce::String& text)
{
    if (label == text) return;
    label = text;
    repaint();
}

void StatusLamp::setState(juce::Colour clr, float newLit)
{
    if (clr == colour && newLit == lit) return;
    colour = clr;
    lit    = newLit;
    repaint();
}

void StatusLamp::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;
    using mu_ui::s;
    using mu_ui::sf;
    const auto& L = MuLookAndFeel::lighting();

    // The lamp: a disc of lamp base with the lit lens 1 px inside it, left-aligned, centred.
    const float d = sf((float) MuLookAndFeel::kStatusLampD);
    const juce::Rectangle<float> disc(0.0f, ((float) getHeight() - d) * 0.5f, d, d);
    g.setColour(MuLookAndFeel::lampBase());
    g.fillEllipse(disc);
    juce::Path lens;
    lens.addEllipse(disc.reduced(1.0f));
    MuLookAndFeel::drawLamp(g, lens, {}, MuLookAndFeel::lampColour(colour, lit), disc.getCentre(),
                            d * L.lampHotSpotReach);

    // The label to its right: engraved on metal, plain text otherwise.
    const auto textArea = getLocalBounds().withTrimmedLeft((int) std::ceil(d) + s(MuLookAndFeel::kSpaceXS));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(11.0f))));
    if (MuLookAndFeel::isMetal(*this))
        MuLookAndFeel::drawEngravedText(g, label, textArea, juce::Justification::centredLeft,
                                        MuLookAndFeel::colour(Id::labelText));
    else
    {
        g.setColour(MuLookAndFeel::colour(Id::labelText));
        g.drawText(label, textArea, juce::Justification::centredLeft, true);
    }
}

void StatusLamp::mouseEnter(const juce::MouseEvent&)
{
    if (onStatusUpdate) onStatusUpdate(statusName, statusText);
}
