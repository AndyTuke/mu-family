#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/MuLookAndFeel.h"

// Family modal-card chrome — the single source of truth for the dimmed backdrop + the
// centred rounded card used by every modal overlay (ModalDialog, SaveDialog, AboutPanel).
// Keeps the dim colour / corner radius / border consistent so a look change is one edit.
namespace mu_ui {

// Fill the whole component with the modal dim. Call first, before painting the card.
inline void fillModalDim(juce::Graphics& g)
{
    g.fillAll(MuLookAndFeel::colour(MuLookAndFeel::backgroundModalDim));
}

// Paint the rounded card panel + 1 px border at `card` (8 px corners, UI-scaled).
inline void paintModalCard(juce::Graphics& g, juce::Rectangle<int> card)
{
    const float r  = mu_ui::sf(8.0f);
    const auto  cf = card.toFloat();
    g.setColour(MuLookAndFeel::colour(MuLookAndFeel::panelBackground));
    g.fillRoundedRectangle(cf, r);
    g.setColour(MuLookAndFeel::colour(MuLookAndFeel::segmentInactiveBorder));
    g.drawRoundedRectangle(cf.reduced(0.5f), r, 1.0f);
}

} // namespace mu_ui
