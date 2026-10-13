#pragma once

#include "Driver/DevicePorts.h"
#include "Surface/SurfaceDescription.h"
#include "Surface/SurfaceEvent.h"
#include <functional>
#include <vector>

// The wire-protocol half of a device: it opens and closes the device, turns input bytes into SurfaceEvents and
// SurfaceEvents' lights into bytes. It knows no ControlAction, no LiveView and no mapping (the boundary check
// keeps Devices/ from including them). One implementation per device family.
namespace mu_control
{

class SurfaceDriver
{
public:
    virtual ~SurfaceDriver() = default;

    virtual const SurfaceDescription& describe() const = 0;

    // Open puts the device into its host-controlled mode over the given byte pipe, which must outlive the open
    // driver; close restores the device's own mode and is harmless when already closed.
    virtual bool open(DevicePorts& ports) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    // One batched send of the lights that should change; a closed driver sends nothing.
    virtual void setLeds(const std::vector<LedState>& leds) = 0;

    // Input: called on the MIDI thread by a real driver (the manager hands off to the message thread).
    std::function<void(const SurfaceEvent&)> onEvent;
};

} // namespace mu_control
