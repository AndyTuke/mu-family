#include "Driver/JuceDevicePorts.h"

namespace mu_control
{

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

bool JuceDevicePorts::open(const PortSpec& spec)
{
    close();

    // First port whose name matches each pattern.
    for (const auto& d : juce::MidiOutput::getAvailableDevices())
        if (d.name.matchesWildcard(spec.outputPattern, true)) { output = juce::MidiOutput::openDevice(d.identifier); break; }

    for (const auto& d : juce::MidiInput::getAvailableDevices())
        if (d.name.matchesWildcard(spec.inputPattern, true)) { input = juce::MidiInput::openDevice(d.identifier, this); break; }

    if (! isOpen()) { close(); return false; }
    input->start();
    return true;
}

void JuceDevicePorts::close()
{
    if (input != nullptr) input->stop();
    input.reset();
    output.reset();
}

bool JuceDevicePorts::send(const uint8_t* data, size_t size)
{
    if (output == nullptr || data == nullptr || size == 0) return false;
    output->sendMessageNow(juce::MidiMessage(data, (int) size));
    return true;
}

void JuceDevicePorts::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message)
{
    if (onBytes) onBytes(message.getRawData(), (size_t) message.getRawDataSize());
}

} // namespace mu_control
