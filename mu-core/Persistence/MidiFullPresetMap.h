#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <atomic>

// 128-entry list of full-preset paths indexed by MIDI program-change number,
// triggered by program changes on MIDI channel 9.
//
// Parallel to MidiPresetMap (which maps PC on channels 1-8 to per-slot
// presets). This map is whole-preset: a PC on channel 9 loads the mapped
// file via the consuming plugin's full-preset load path (deferred to a loop
// point in mu-clid; semantics defined by the plugin).
//
// The persistence target is plugin-specific — the consuming plugin calls
// setStorageFile() once at startup before load(); mutations auto-save.
// File extension is plugin-defined.
class MidiFullPresetMap
{
public:
    static constexpr int NumSlots = 128;
    static constexpr int Channel  = 9;   // fixed trigger channel (1-based MIDI channel)

    // Configure persistence target. Call BEFORE load() and BEFORE any
    // mutation. After this is set, mutations auto-save.
    void setStorageFile(juce::File f);

    juce::String getPresetPath(int index) const;
    void         setPresetPath(int index, const juce::File& f);
    void         clearPreset(int index);
    bool         hasPreset(int index) const;

    bool isEnabled() const { return enabled.load(std::memory_order_relaxed); }
    void setEnabled(bool e);

    void load();
    void save() const;

private:
    juce::File storageFile;
    std::array<juce::String, NumSlots> paths;
    std::atomic<bool> enabled { true };
};
