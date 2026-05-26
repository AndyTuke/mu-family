#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "HotSwapStager.h"   // HotSwapStager::PreparedFullPreset (commit payload)

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

    // Entry point. When the sequencer is playing, a full .muclid preset is staged
    // for commit at the next master loop boundary (matching per-rhythm hot-swap).
    // When stopped — or for a non-preset host-state restore — it applies now via
    // applyPresetImmediate.
    void loadPreset(const juce::File& file);

    // The synchronous load body for the stopped path (and non-preset host-state
    // restores). Parses + applies the preset immediately on the calling thread.
    void applyPresetImmediate(const juce::File& file);

    // Commit a pre-built full preset (prepared by loadPreset's playing path) into
    // live state. Called from HotSwapStager::processSwaps at the loop boundary:
    // installs the pre-built voices/rhythms under a single suspend, then finalises
    // the APVTS / mixer / global params. `prepared` is consumed (voices moved out).
    void commitStagedFullPreset(HotSwapStager::PreparedFullPreset& prepared);

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

    // Restore the APVTS rhythm-param block for slot `apvtsSlot` from `rTree`.
    // `srcPropPrefix` is prepended to each property name when reading from the
    // tree (e.g. "" for .muclid per-rhythm subtree, "r0_" for .muRhyth root).
    void restoreRhythmAPVTSParams(int apvtsSlot, const juce::ValueTree& rTree,
                                   const juce::String& srcPropPrefix);

    // Restore the channel-strip params for slot `apvtsSlot`. Only relevant for
    // the .muclid format — legacy .muRhyth `ch_*` properties are intentionally
    // ignored by applyRhythmPreset (see Mixer-state-stays-with-slot policy).
    void restoreRhythmChannelParams(int apvtsSlot, const juce::ValueTree& rTree);

    // Restore embedded-sample bytes (preferred) or stored sample path for slot.
    // The three property-name args spell out exactly which keys to read so the
    // helper can serve unprefixed .muclid subtrees, "r0_"-prefixed .muRhyth
    // roots, and "r{i}_"-prefixed host-state roots from a single body. Fires
    // onLoadError if the linked sample is missing.
    void restoreRhythmSample(int slot, const juce::ValueTree& tree,
                              const juce::String& samplePathProp,
                              const juce::String& sampleDataProp,
                              const juce::String& sampleNameProp);

    // Deserialise the optional <Modulators> child for slot `i`. No-op if absent
    // (legacy presets leave the rhythm's in-memory modulators intact).
    void restoreRhythmModulators(int i, const juce::ValueTree& rTree);

    // Restore the <GlobalState> child if present (mixer + FX algorithm params).
    void restoreGlobalState(const juce::ValueTree& root);
};
