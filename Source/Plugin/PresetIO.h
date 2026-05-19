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

    // Hot-swap staging (pending move to HotSwapStager in #475).
    void stageRhythmPreset(int rhythmIndex, const juce::File& file);
    void cancelStagedSwap(int rhythmIndex);
    bool hasPendingSwap(int rhythmIndex) const;

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
};
