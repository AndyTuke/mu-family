#pragma once

#include "Driver/DevicePorts.h"
#include "Driver/SurfaceDriver.h"
#include <functional>
#include <memory>

// What the manager needs to run one device: the byte pipe, the driver on it, and the port names that tell the
// device is present without opening anything. A test supplies fakes; production supplies a real device from a
// Devices/ folder through makeDefaultBackend, so the manager names no device.
namespace mu_control
{

struct ControllerBackend
{
    std::unique_ptr<DevicePorts>   ports;
    std::unique_ptr<SurfaceDriver> driver;
    PortSpec                       detect;   // matched against outputNames() / inputNames() to find the device
};

using ControllerBackendFactory = std::function<ControllerBackend()>;

// The device this build supports (defined in Devices/; the Launchpad X today).
ControllerBackend makeDefaultBackend();

} // namespace mu_control
