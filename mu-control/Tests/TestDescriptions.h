#pragma once

#include "Control/SurfaceLook.h"
#include "Surface/SurfaceDescription.h"

// The surface descriptions the model tests run against, and the look they use. None is a real driver's: a
// Launchpad-shaped surface (so the default mapping is the shipped one) and the shapes the mock grid and a
// native-blink surface give, to prove the model asks the description and never a device name.
namespace mu_control::test
{

inline SurfaceDescription launchpadStyle()
{
    SurfaceDescription d;
    d.id = "test-launchpad-style"; d.name = "Launchpad-style";
    d.gridWidth = 8; d.gridHeight = 8;
    d.buttons = { { ButtonKind::Top, 8, "Top" }, { ButtonKind::Side, 8, "Side" }, { ButtonKind::Special, 1, "Logo" } };
    d.colour = ColourSupport::Rgb; d.canFlash = true; d.canPulse = true; d.hasPressure = true;
    return d;
}

// 4 x 4 grid, 4 top buttons, no side buttons, palette colour only, no flash or pulse (MockGridDriver::describe()).
inline SurfaceDescription mockGrid()
{
    SurfaceDescription d;
    d.id = "test-mock-grid"; d.name = "Mock grid";
    d.gridWidth = 4; d.gridHeight = 4;
    d.buttons = { { ButtonKind::Top, 4, "Top" } };
    d.colour = ColourSupport::Palette;
    return d;
}

// Palette colour with native pulse: the model must hand the blink to the device (LedMode::Pulse).
inline SurfaceDescription nativeBlink()
{
    SurfaceDescription d = mockGrid();
    d.id = "test-native-blink"; d.name = "Native blink";
    d.canFlash = true; d.canPulse = true;
    return d;
}

inline mu_core::SurfaceLook testLook()
{
    mu_core::SurfaceLook l;
    for (size_t i = 0; i < l.layerRgb.size(); ++i)
        l.layerRgb[i] = 0x200000u + (uint32_t) i * 0x101010u;   // distinct per palette index
    l.accentRgb = 0xFF8000;
    l.faultRgb  = 0xFF0000;
    l.goRgb     = 0x00FF00;
    l.whiteRgb  = 0xFFFFFF;
    l.dimScale  = 0.30f;
    l.pulseHz   = 2.0f;
    l.flashHz   = 1.0f;
    l.stepFlashMs = 150;
    return l;
}

} // namespace mu_control::test
