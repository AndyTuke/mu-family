#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <atomic>
#include <cstdint>

// 128-entry list of per-slot preset paths indexed by MIDI program-change
// number. Channel mask (bits 0-7 = MIDI channels 1-8) gates which channels
// load presets. The persistence target is plugin-specific — the consuming
// plugin calls setStorageFile() once at startup before load(), then every
// mutation auto-saves to that file.
//
// The map only stores strings; the referenced file extension is decided by
// the plugin (mu-clid uses `.muRhyth`; mu-tant / mu-toni will define their
// own per-slot preset formats).
class MidiPresetMap
{
public:
    static constexpr int NumSlots = 128;

    // Configure persistence target. Call BEFORE load() and BEFORE any
    // mutation. After this is set, mutations auto-save.
    void setStorageFile(juce::File f);

    juce::String getPresetPath(int index) const;
    void         setPresetPath(int index, const juce::File& f);
    void         clearPreset(int index);
    bool         hasPreset(int index) const;

    uint8_t getChannelMask() const { return channelMask.load(std::memory_order_relaxed); }
    void    setChannelMask(uint8_t mask);

    void load();
    void save() const;

private:
    juce::File storageFile;
    std::array<juce::String, NumSlots> paths;
    std::atomic<uint8_t> channelMask { 0xFF };
};
