#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <atomic>
#include <cstdint>

// 128-entry list of .muRhyth file paths indexed by MIDI program-change number.
// Channel mask (bits 0-7 = MIDI channels 1-8) gates which channels load presets.
// Persisted independently of .muclid as JSON in the same folder as appSettings
// (e.g. %APPDATA%\TDP\muClid_midiPresets.json on Windows).
class MidiPresetMap
{
public:
    static constexpr int NumSlots = 128;

    juce::String getPresetPath(int index) const;
    void         setPresetPath(int index, const juce::File& f);
    void         clearPreset(int index);
    bool         hasPreset(int index) const;

    uint8_t getChannelMask() const { return channelMask.load(std::memory_order_relaxed); }
    void    setChannelMask(uint8_t mask);

    void load();
    void save() const;

    static juce::File getDefaultFile();

private:
    std::array<juce::String, NumSlots> paths;
    std::atomic<uint8_t> channelMask { 0xFF };
};
