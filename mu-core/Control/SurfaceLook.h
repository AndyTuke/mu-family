#pragma once

#include <array>
#include <cstdint>

// The colours and timings a control surface (mu-control) needs, as plain data. The standalone shell fills
// it from the same tokens the screen uses (the layer palette, the app accent, the fault colour, the
// Lighting pad values), so a pad is the colour of its layer on screen and mu-control includes no UI header.
// Colours are 0xRRGGBB.
namespace mu_core
{

struct SurfaceLook
{
    std::array<uint32_t, 8> layerRgb {};          // layer colour by palette index (LiveView::getLayerColourIndex)
    uint32_t                accentRgb = 0;        // the app accent: performance items
    uint32_t                faultRgb  = 0;        // a missing or foreign item
    float                   dimScale  = 0.45f;    // dim state = bright colour x this (chosen by eye on a real Launchpad X)
    float                   pulseHz   = 2.0f;     // queued: smooth dim to bright
    float                   flashHz   = 1.0f;     // muted playing clip: square bright / dim
    int                     stepFlashMs = 150;    // a newly playing clip's one bright flash
};

} // namespace mu_core
