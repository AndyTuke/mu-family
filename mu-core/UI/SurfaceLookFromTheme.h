#pragma once

#include "Control/SurfaceLook.h"
#include "UI/Components/MuLookAndFeel.h"

// The control-surface colours and timings, read from the tokens the screen uses so a pad is the colour of its
// layer on screen. Call after the editor has set the app accent (the theme is per binary).
namespace mu_ui
{

inline mu_core::SurfaceLook makeSurfaceLook()
{
    using Id = MuLookAndFeel::ColourIds;
    const auto rgb = [](juce::Colour c) { return (uint32_t) (c.getARGB() & 0x00ffffffu); };
    const auto& light = MuLookAndFeel::lighting();

    mu_core::SurfaceLook look;
    for (size_t i = 0; i < look.layerRgb.size(); ++i)
        look.layerRgb[i] = rgb(MuLookAndFeel::layerPalette[i % (size_t) MuLookAndFeel::kLayerPaletteSize]);
    look.accentRgb   = rgb(MuTheme::current().global.accent);
    look.faultRgb    = rgb(MuLookAndFeel::colour(Id::indicatorFault));
    look.goRgb       = rgb(MuTheme::current().surface.go);
    look.whiteRgb    = rgb(MuTheme::current().surface.white);
    look.dimScale    = light.padDimScale;
    look.pulseHz     = light.queuedPulseHz;
    look.stepFlashMs = (int) light.stepFlashMs;
    look.flashHz     = light.clipMutedFlashHz;
    return look;
}

} // namespace mu_ui
