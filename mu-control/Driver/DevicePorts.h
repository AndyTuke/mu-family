#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <juce_core/juce_core.h>
#include <vector>

// The byte pipe a driver sits on: one output for commands and lights, one input for pad and button messages.
// Drivers never touch a JUCE MIDI port type, so they test against a fake with no hardware. The real
// implementation is JuceDevicePorts; the fake is in Tests/.
namespace mu_control
{

// Which ports to open, as wildcard patterns on the port name ('*' and '?'), case-insensitive.
struct PortSpec
{
    juce::String outputPattern;
    juce::String inputPattern;
};

class DevicePorts
{
public:
    virtual ~DevicePorts() = default;

    virtual juce::StringArray outputNames() const = 0;
    virtual juce::StringArray inputNames() const = 0;

    // Opens the first output and input matching the spec; false (and nothing left open) when either is missing.
    virtual bool open(const PortSpec& spec) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    // True while the opened device is still attached (an unplugged device stays open but not connected).
    virtual bool isConnected() const = 0;

    // Sends one complete MIDI message (a short message or a whole SysEx F0..F7); false when it did not go out.
    virtual bool send(const uint8_t* data, size_t size) = 0;
    bool sendMessage(const std::vector<uint8_t>& bytes) { return send(bytes.data(), bytes.size()); }

    // Raw bytes of one received message, called on the MIDI thread.
    std::function<void(const uint8_t* data, size_t size)> onBytes;

    // The device vanished (false) or came back (true), called on the thread that noticed.
    std::function<void(bool connected)> onConnectionChanged;
};

} // namespace mu_control
