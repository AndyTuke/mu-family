#include "Devices/LaunchpadX/LaunchpadXDriver.h"
#include "Driver/ControllerBackend.h"
#include "Driver/JuceDevicePorts.h"

namespace mu_control
{

ControllerBackend makeDefaultBackend()
{
    return { std::make_unique<JuceDevicePorts>(), std::make_unique<LaunchpadXDriver>(), LaunchpadXDriver::defaultPorts() };
}

} // namespace mu_control
