#include "Driver/JuceDevicePorts.h"

namespace mu_control
{

namespace
{
constexpr int kWatchMs  = 500;    // how often the opened device's presence is checked
constexpr int kSettleMs = 1000;   // after a replug the pad needs a moment to boot before it accepts setup messages
}

juce::StringArray JuceDevicePorts::outputNames() const
{
    juce::StringArray names;
    for (const auto& d : juce::MidiOutput::getAvailableDevices()) names.add(d.name);
    return names;
}

juce::StringArray JuceDevicePorts::inputNames() const
{
    juce::StringArray names;
    for (const auto& d : juce::MidiInput::getAvailableDevices()) names.add(d.name);
    return names;
}

bool JuceDevicePorts::openPorts(const PortSpec& spec)
{
    // First port whose name matches each pattern.
    for (const auto& d : juce::MidiOutput::getAvailableDevices())
        if (d.name.matchesWildcard(spec.outputPattern, true)) { output = juce::MidiOutput::openDevice(d.identifier); outputId = d.identifier; break; }

    for (const auto& d : juce::MidiInput::getAvailableDevices())
        if (d.name.matchesWildcard(spec.inputPattern, true)) { input = juce::MidiInput::openDevice(d.identifier, this); inputId = d.identifier; break; }

    if (! isOpen()) { closePorts(); return false; }
    input->start();
    return true;
}

void JuceDevicePorts::closePorts()
{
    if (input != nullptr) input->stop();
    input.reset();
    output.reset();
}

bool JuceDevicePorts::open(const PortSpec& spec)
{
    close();
    if (! openPorts(spec)) return false;
    openSpec = spec;
    attached = true;
    startTimer(kWatchMs);
    return true;
}

void JuceDevicePorts::close()
{
    stopTimer();
    closePorts();
    attached = false;
}

void JuceDevicePorts::timerCallback()
{
    // Is the opened pair still in the system's lists?
    const auto listed = [](const auto& devices, const juce::String& id)
    {
        for (const auto& d : devices) if (d.identifier == id) return true;
        return false;
    };
    const bool present = listed(juce::MidiOutput::getAvailableDevices(), outputId)
                      && listed(juce::MidiInput::getAvailableDevices(), inputId);
    if (! present) settleUntilMs = 0;   // gone again before it settled
    if (present == attached) return;

    if (present)
    {
        // It is in the lists as soon as USB enumerates, but a Launchpad ignores setup messages while it boots: wait first.
        const auto now = juce::Time::getMillisecondCounter();
        if (settleUntilMs == 0) { settleUntilMs = (juce::int64) now + kSettleMs; return; }
        if ((juce::int64) now < settleUntilMs) return;
        settleUntilMs = 0;

        // Settled: the old handles are dead, so open a fresh pair before saying so (retry on the next tick if it fails).
        closePorts();
        if (! openPorts(openSpec)) return;
    }
    attached = present;
    if (onConnectionChanged) onConnectionChanged(present);
}

bool JuceDevicePorts::send(const uint8_t* data, size_t size)
{
    if (output == nullptr || ! attached || data == nullptr || size == 0) return false;
    output->sendMessageNow(juce::MidiMessage(data, (int) size));
    return true;
}

void JuceDevicePorts::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message)
{
    if (onBytes) onBytes(message.getRawData(), (size_t) message.getRawDataSize());
}

} // namespace mu_control
