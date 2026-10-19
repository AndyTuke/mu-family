#include "Devices/LaunchpadX/LaunchpadXDriver.h"
#include "Devices/LaunchpadX/LaunchpadXColourMap.h"
#include "Devices/LaunchpadX/LaunchpadXProtocol.h"

namespace mu_control
{

LaunchpadXDriver::LaunchpadXDriver(bool rgbLighting) : rgb(rgbLighting)
{
    description.id = "novation-launchpad-x";
    description.name = "Launchpad X";
    description.gridWidth = 8;
    description.gridHeight = 8;
    description.buttons = { { ButtonKind::Top, 8, "Top" }, { ButtonKind::Side, 8, "Side" }, { ButtonKind::Special, 1, "Logo" } };
    description.colour = rgb ? ColourSupport::Rgb : ColourSupport::Palette;
    description.canFlash = true;
    description.canPulse = true;
    description.hasPressure = false;   // polyphonic aftertouch is switched off
}

PortSpec LaunchpadXDriver::defaultPorts()
{
    return { "LPX MIDI", "MIDIIN2 (LPX MIDI)" };
}

bool LaunchpadXDriver::open(DevicePorts& devicePorts)
{
    if (ports != nullptr) return true;
    if (! devicePorts.open(defaultPorts())) return false;

    ports = &devicePorts;
    linkUp = true;
    sent.clear();
    desired.clear();

    // Input is delivered on the message thread (ManagedPorts moves it off the MIDI thread) and goes straight out as SurfaceEvents.
    ports->onBytes = [this](const uint8_t* data, size_t size)
    {
        for (const auto& e : launchpadx::decodeInput(data, size))
            if (onEvent) onEvent(e);
    };
    ports->onConnectionChanged = [this](bool connected) { onConnectionChanged(connected); };

    if (! sendSetup())
    {
        close();
        return false;
    }
    return true;
}

void LaunchpadXDriver::close()
{
    if (ports == nullptr) return;

    // Pads off, then the device's own layout back; an unplugged device cannot be told, and needs neither.
    if (linkUp)
    {
        std::vector<LedState> off;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) off.push_back({ { ControlKind::Pad, x, y }, 0, LedMode::Static });
        for (int i = 0; i < 8; ++i) off.push_back({ { ControlKind::Top, i, 0 }, 0, LedMode::Static });
        for (int i = 0; i < 8; ++i) off.push_back({ { ControlKind::Side, 0, i }, 0, LedMode::Static });
        sendLights(off);
        ports->sendMessage(launchpadx::programmerMode(false));
        ports->flush();   // the Live-mode message must reach the pad before the port is torn down
    }

    // Close first (it stops the input thread), then drop the callbacks: clearing them while the MIDI thread
    // may still be inside one would race.
    ports->close();
    ports->onBytes = nullptr;
    ports->onConnectionChanged = nullptr;
    ports = nullptr;
    linkUp = false;
    sent.clear();
    desired.clear();
}

bool LaunchpadXDriver::sendSetup()
{
    // Programmer mode first, then aftertouch off (it is on by default in Programmer mode).
    return ports->sendMessage(launchpadx::programmerMode(true)) && ports->sendMessage(launchpadx::aftertouchOff());
}

bool LaunchpadXDriver::sendLights(const std::vector<LedState>& leds)
{
    std::vector<launchpadx::PadLight> lights;
    for (const auto& l : leds)
        if (const auto index = launchpadx::indexOf(l.id))   // the logo has no index and stays unlit
            lights.push_back({ *index, l.mode, rgb ? launchpadx::padRgbFor(l.rgb) : l.rgb });   // RGB lighting gets the pad's colour correction
    if (lights.empty()) return true;

    bool ok = true;
    for (const auto& m : launchpadx::lightingMessages(lights, rgb)) ok = ports->sendMessage(m) && ok;
    return ok;
}

void LaunchpadXDriver::setLeds(const std::vector<LedState>& leds)
{
    if (ports == nullptr) return;

    // Remember the whole frame (for a replug) and pick out the lights that differ from what the pad shows.
    std::vector<LedState> changed;
    for (auto led : leds)
    {
        if (! launchpadx::indexOf(led.id)) continue;
        if (rgb) led.mode = LedMode::Static;   // RGB colours are final, the model does the blinking
        desired[led.id] = led;
        const auto it = sent.find(led.id);
        if (linkUp && (it == sent.end() || it->second != led)) changed.push_back(led);
    }
    if (changed.empty()) return;

    if (sendLights(changed))
        for (const auto& led : changed) sent[led.id] = led;
}

void LaunchpadXDriver::onConnectionChanged(bool connected)
{
    if (ports == nullptr || connected == linkUp) return;
    linkUp = connected;
    sent.clear();
    if (! connected) return;

    // Replug: the pad came back in Live mode with dark pads, so repeat the setup and the whole last frame.
    if (! sendSetup()) { linkUp = false; return; }
    std::vector<LedState> frame;
    for (const auto& kv : desired) frame.push_back(kv.second);
    if (sendLights(frame))
        for (const auto& led : frame) sent[led.id] = led;
}

} // namespace mu_control
