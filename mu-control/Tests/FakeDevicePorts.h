#pragma once

#include "Driver/DevicePorts.h"
#include <vector>

// A pipe with no hardware: records every sent message, lets a test inject received bytes, and simulates an
// unplug (sends then fail and nothing is recorded) and a replug.
namespace mu_control::test
{

class FakeDevicePorts : public DevicePorts
{
public:
    juce::StringArray outputNames() const override { return outputs; }
    juce::StringArray inputNames() const override  { return inputs; }

    bool open(const PortSpec& spec) override
    {
        lastSpec = spec;
        ++openCalls;
        if (! connected) return false;
        const auto has = [](const juce::StringArray& names, const juce::String& pattern)
        {
            for (const auto& n : names) if (n.matchesWildcard(pattern, true)) return true;
            return false;
        };
        opened = has(outputs, spec.outputPattern) && has(inputs, spec.inputPattern);
        return opened;
    }
    void close() override  { if (opened) ++closeCalls; opened = false; }
    bool isOpen() const override      { return opened; }
    bool isConnected() const override { return opened && connected; }

    bool send(const uint8_t* data, size_t size) override
    {
        if (! isConnected()) return false;
        sent.emplace_back(data, data + size);
        return true;
    }

    void inject(const std::vector<uint8_t>& bytes) { if (opened && onBytes) onBytes(bytes.data(), bytes.size()); }

    void unplug()
    {
        if (! connected) return;
        connected = false;
        if (opened && onConnectionChanged) onConnectionChanged(false);
    }
    void replug()
    {
        if (connected) return;
        connected = true;
        if (opened && onConnectionChanged) onConnectionChanged(true);
    }

    juce::StringArray outputs { "LPX MIDI", "MIDIOUT2 (LPX MIDI)" };
    juce::StringArray inputs  { "LPX MIDI", "MIDIIN2 (LPX MIDI)" };
    std::vector<std::vector<uint8_t>> sent;
    PortSpec lastSpec;
    int  openCalls = 0, closeCalls = 0;
    bool connected = true;

private:
    bool opened = false;
};

} // namespace mu_control::test
