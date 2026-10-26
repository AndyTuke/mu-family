#pragma once
#include "MuLookAndFeel.h"

namespace mu_ui
{

// One numbered lamp pad: the single look shared by the layer's ClipStrip and the sidebar's SceneGrid
// (docs/design-ui-family.md 13.2 and 13.9), so a pad reads the same wherever it appears.
//   cell  the pad's whole rectangle; `hue` the lamp colour; `lit` the lit amount from lampLit().
// A dark base with the rounded lens a pixel inside it, lit round a hot spot at its centre, the number engraved on top:
// muted grey on an unlit or dim lens, the dark base colour once the lens is lit so it stays readable.
inline void drawNumberedPad(juce::Graphics& g, juce::Rectangle<int> cell, int number, juce::Colour hue, float lit)
{
    const auto& L = MuLookAndFeel::lighting();
    const float corner = sf((float) MuLookAndFeel::kLampCellCorner);
    const auto  cellF  = cell.toFloat();

    g.setColour(MuLookAndFeel::lampBase());
    g.fillRoundedRectangle(cellF, corner);
    juce::Path lens;
    lens.addRoundedRectangle(cellF.reduced(1.0f), corner);
    MuLookAndFeel::drawLamp(g, lens, {}, MuLookAndFeel::lampColour(hue, lit), cellF.getCentre(),
                            juce::jmax(cellF.getWidth(), cellF.getHeight()) * L.lampHotSpotReach);

    const auto ink = lit >= L.lampOn ? MuLookAndFeel::lampBase()
                                     : MuLookAndFeel::colour(MuLookAndFeel::ColourIds::mutedText);
    MuLookAndFeel::drawEngravedText(g, juce::String(number), cell, juce::Justification::centred, ink);
}

} // namespace mu_ui
