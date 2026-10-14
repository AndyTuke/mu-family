#pragma once

#include "Driver/DevicePorts.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>
#include <memory>

// DevicePorts over juce::MidiOutput / juce::MidiInput. While open it polls the port lists twice a second on
// the message thread: when the opened device disappears it reports onConnectionChanged(false), and when it
// returns it reopens the ports (the old handles died with the unplug) and reports true. Create, open and close
// it on the message thread.
namespace mu_control
{

class JuceDevicePorts : public DevicePorts, private juce::MidiInputCallback, private juce::Timer
{
public:
    JuceDevicePorts() = default;
    ~JuceDevicePorts() override { close(); }

    juce::StringArray outputNames() const override;
    juce::StringArray inputNames() const override;

    bool open(const PortSpec& spec) override;
    void close() override;
    bool isOpen() const override      { return output != nullptr && input != nullptr; }
    bool isConnected() const override { return isOpen() && attached; }

    bool send(const uint8_t* data, size_t size) override;

private:
    bool openPorts(const PortSpec& spec);
    void closePorts();
    void timerCallback() override;
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) override;

    PortSpec                          openSpec;
    juce::String                      outputId, inputId;   // identifiers of the opened pair, for the presence check
    bool                              attached = false;
    juce::int64                       settleUntilMs = 0;   // after a replug: when the device may be trusted again (0 = not waiting)
    std::unique_ptr<juce::MidiOutput> output;
    std::unique_ptr<juce::MidiInput>  input;
};

} // namespace mu_control
