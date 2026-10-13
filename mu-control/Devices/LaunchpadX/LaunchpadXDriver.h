#pragma once

#include "Driver/SurfaceDriver.h"
#include <map>

// The Novation Launchpad X over a DevicePorts byte pipe: Programmer mode while open (aftertouch off), pad and
// button bytes in, one batched lighting SysEx out, Live mode on every close path including the destructor.
// Lighting sends only what changed since the last send; an unplug marks the link down and a replug repeats
// the setup and the whole last frame. Lighting is RGB; `rgbLighting = false` selects the palette form (with the
// device's own flash and pulse) for a unit that cannot take RGB.
namespace mu_control
{

class LaunchpadXDriver : public SurfaceDriver
{
public:
    explicit LaunchpadXDriver(bool rgbLighting = true);
    ~LaunchpadXDriver() override { close(); }

    const SurfaceDescription& describe() const override { return description; }

    // The byte pipe must outlive the open driver. Opens the ports, then sends Programmer mode and aftertouch off.
    bool open(DevicePorts& devicePorts) override;
    void close() override;
    bool isOpen() const override { return ports != nullptr; }

    void setLeds(const std::vector<LedState>& leds) override;

    // True while the device is attached; an open driver whose device vanished keeps its last frame for the replug.
    bool isLinkUp() const noexcept { return ports != nullptr && linkUp; }

    // Where the pad answers: the first output for commands and lights, the second input for pads and buttons.
    static PortSpec defaultPorts();

private:
    bool sendSetup();
    bool sendLights(const std::vector<LedState>& leds);
    void onConnectionChanged(bool connected);

    SurfaceDescription description;
    bool               rgb;
    DevicePorts*       ports  = nullptr;
    bool               linkUp = false;
    std::map<SurfaceControlId, LedState> desired;   // the last frame asked for
    std::map<SurfaceControlId, LedState> sent;      // what the device is known to show
};

} // namespace mu_control
