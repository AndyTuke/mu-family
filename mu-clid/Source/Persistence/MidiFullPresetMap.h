#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>  // juce::PropertiesFile (getDefaultFile)
#include <array>
#include <atomic>

// 128-entry list of full-session `.muclid` preset paths indexed by MIDI
// program-change number, triggered by program changes on MIDI channel 9.
//
// Parallel to MidiPresetMap (which maps program changes on channels 1-8 to
// per-rhythm `.muRhyth` presets, one channel per rhythm slot). This map is
// whole-preset: a PC on channel 9 loads the mapped `.muclid` via the deferred /
// prestaged full-preset hot-swap path, so it lands at the next loop point.
//
// Persisted independently of the rhythm map as JSON alongside it
// (e.g. %APPDATA%\TDP\muClid_midiFullPresets.json on Windows).
class MidiFullPresetMap
{
public:
    static constexpr int NumSlots = 128;
    static constexpr int Channel  = 9;   // fixed trigger channel (1-based MIDI channel)

    juce::String getPresetPath(int index) const;
    void         setPresetPath(int index, const juce::File& f);
    void         clearPreset(int index);
    bool         hasPreset(int index) const;

    bool isEnabled() const { return enabled.load(std::memory_order_relaxed); }
    void setEnabled(bool e);

    void load();
    void save() const;

    static juce::File getDefaultFile();

private:
    std::array<juce::String, NumSlots> paths;
    std::atomic<bool> enabled { true };
};
