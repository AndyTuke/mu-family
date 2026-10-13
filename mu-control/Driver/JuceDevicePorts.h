#pragma once

#include "Driver/DevicePorts.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <memory>

// DevicePorts over juce::MidiOutput / juce::MidiInput. Thin on purpose: no hot-plug watching yet, so
// onConnectionChanged is never raised and isConnected() is simply isOpen().
namespace mu_control
{

class JuceDevicePorts : public DevicePorts, private juce::MidiInputCallback
{
public:
    JuceDevicePorts() = default;
    ~JuceDevicePorts() override { close(); }

    juce::StringArray outputNames() const override;
    juce::StringArray inputNames() const override;

    bool open(const PortSpec& spec) override;
    void close() override;
    bool isOpen() const override      { return output != nullptr && input != nullptr; }
    bool isConnected() const override { return isOpen(); }

    bool send(const uint8_t* data, size_t size) override;

private:
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) override;

    std::unique_ptr<juce::MidiOutput> output;
    std::unique_ptr<juce::MidiInput>  input;
};

} // namespace mu_control
