#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

class PluginProcessor;

// Encapsulates all preset save/load logic extracted from PluginProcessor.
// Declared as friend of PluginProcessor so it can access private members
// (apvts, sequencer, voiceEngines, pendingSwaps, etc.) via proc_.
// PluginProcessor keeps thin public delegates to the methods below.
class PresetIO
{
public:
    explicit PresetIO(PluginProcessor& proc) : proc_(proc) {}

    // Hot-swap staging: loads a preset file and stages it via HotSwapStager.
    void stageRhythmPreset(int rhythmIndex, const juce::File& file);

    // Preset category list.
    juce::StringArray loadCategoryList() const;
    void              ensureCategoryInList(const juce::String& cat);

    // Per-rhythm preset I/O.
    void saveRhythmPresetToFile(int rhythmIdx, const juce::File& destFile,
                                bool embedSample = false,
                                const juce::String& category = {},
                                const juce::String& description = {});
    bool applyRhythmPreset(const juce::File& file, int rhythmIndex);
    bool applyDefaultRhythm(int rhythmIndex);
    void loadDefaultPreset();

    // Full project preset I/O.
    void savePreset(const juce::String& name, const juce::String& description,
                    const juce::String& category, bool embedSamples);
    void loadPreset(const juce::File& file);

    // JUCE AudioProcessor state (called from PluginProcessor's overrides).
    void getStateInformation(juce::MemoryBlock& destData);
    void restoreStateFromTree(const juce::ValueTree& state);
    void setStateInformation(const void* data, int sizeInBytes);

private:
    PluginProcessor& proc_;

    // Helpers extracted from loadPreset / applyRhythmPreset / restoreStateFromTree
    // so each top-level function reads as a sequence of named steps. All run on
    // the message thread under a single mu_core::ScopedApvtsLoading guard managed
    // by the caller.

    // Resize the sequencer + voice/MIDI/mixer arrays to `n` active rhythms.
    // Pre: `n` already clamped to [1, MaxRhythms]. Post: numActiveRhythms == n.
    void resizeRhythmArrays(int n);

    // Restore APVTS rhythm params + channel params for slot `i` from `rTree`.
    void restoreRhythmParams(int i, const juce::ValueTree& rTree);

    // Restore embedded-sample bytes (preferred) or stored sample path for slot `i`.
    // Fires onLoadError if the linked sample is missing.
    void restoreRhythmSample(int i, const juce::ValueTree& rTree);

    // Deserialise the optional <Modulators> child for slot `i`. No-op if absent
    // (legacy presets leave the rhythm's in-memory modulators intact).
    void restoreRhythmModulators(int i, const juce::ValueTree& rTree);

    // Restore the <GlobalState> child if present (mixer + FX algorithm params).
    void restoreGlobalState(const juce::ValueTree& root);
};
