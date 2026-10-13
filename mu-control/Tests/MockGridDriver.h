#pragma once

#include "Driver/SurfaceDriver.h"
#include "TestDescriptions.h"

// A test-only second driver: a 4 x 4 grid, 4 top buttons, no side buttons, palette colour only, no flash or
// pulse, and a different coordinate origin (bottom-right, counted in the device's own numbering) from the
// Launchpad X. If the same SurfaceModel and SurfaceMapping drive it and a Launchpad-shaped surface with no
// device test anywhere in Surface/, the surface layer is device independent.
namespace mu_control::test
{

class MockGridDriver : public SurfaceDriver
{
public:
    const SurfaceDescription& describe() const override { return description; }

    bool open() override  { opened = true; return true; }
    void close() override { opened = false; }
    bool isOpen() const override { return opened; }

    void setLeds(const std::vector<LedState>& leds) override
    {
        if (! opened) return;   // a closed driver sends nothing
        sent.push_back(leds);
    }

    // The device's own numbering: origin at the bottom-right, x leftwards, y upwards.
    static SurfaceControlId padFromDevice(int dx, int dy) noexcept { return { ControlKind::Pad, 3 - dx, 3 - dy }; }
    static SurfaceControlId topFromDevice(int di) noexcept         { return { ControlKind::Top, 3 - di, 0 }; }

    // Input as the device would report it, turned into surface events.
    void devicePad(int dx, int dy, bool down)
    {
        if (opened && onEvent) onEvent({ padFromDevice(dx, dy), down ? SurfaceEventType::Press : SurfaceEventType::Release, 0.0f });
    }
    void deviceTop(int di, bool down)
    {
        if (opened && onEvent) onEvent({ topFromDevice(di), down ? SurfaceEventType::Press : SurfaceEventType::Release, 0.0f });
    }

    std::vector<std::vector<LedState>> sent;   // every batch setLeds was given while open

private:
    SurfaceDescription description = mockGrid();
    bool               opened = false;
};

} // namespace mu_control::test
