#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Plugin/HotSwapStager.h"   // HotSwapStager::PreparedFullPreset (commit payload)

namespace mu_clid {

class PluginProcessor;

// Encapsulates all preset save/load logic extracted from PluginProcessor.
// Declared as friend of PluginProcessor so it can access private members
// (apvts, sequencer, voiceEngines, pendingSwaps, etc.) via proc_.
// PluginProcessor keeps thin public delegates to the methods below.
class PresetIO
{
public:
    explicit PresetIO(PluginProcessor& proc) : proc_(proc) {}

    // One rhythm prepared off the audio thread, ready to install into a slot.
    struct PreparedRhythm
    {
        Rhythm                       rhythm;
        std::unique_ptr<VoiceEngine> voice;   // null when the preset couldn't be read
        juce::String                 samplePath;
    };

    // Build a rhythm from a preset node: a .muRhythm root (prefix "r0_") or a .muClid <Rhythm>
    // child (no prefix). Defaults for what the node lacks; `source` names it in load messages.
    static PreparedRhythm prepareRhythm(const juce::ValueTree& node, const juce::String& prefix,
                                        double sampleRate, int blockSize, const juce::File& samplesDir,
                                        const std::function<void(const juce::String&)>& onLoadError,
                                        const juce::String& source);

    // Hot-swap staging: loads a preset file and stages it via HotSwapStager.
    // keepIdentity: the slot keeps its name + colour (a settings reset such as the default
    // rhythm, rather than loading a named preset into it).
    void stageRhythmPreset(int rhythmIndex, const juce::File& file, bool keepIdentity = false);

    // Preset category list.
    juce::StringArray loadCategoryList() const;
    void              ensureCategoryInList(const juce::String& cat);

    // Per-rhythm preset I/O.
    void saveRhythmPresetToFile(int rhythmIndex, const juce::File& destFile,
                                bool embedSample = false,
                                const juce::String& category = {},
                                const juce::String& description = {});
    bool applyRhythmPreset(const juce::File& file, int rhythmIndex, bool keepIdentity = false);
    bool applyDefaultRhythm(int rhythmIndex);
    void loadDefaultPreset();

    // Full project preset I/O.
    void savePreset(const juce::String& name, const juce::String& description,
                    const juce::String& category, bool embedSamples);
    bool saveFullPresetTo(const juce::File& file);   // headless render --save-preset

    // Entry point for a full preset / host-state file. Parses, then routes by type:
    // a .muclid full preset is pre-built off the audio thread and committed via
    // commitStagedFullPreset — deferred to the next loop point when playing, applied
    // immediately when stopped (one unified path). A non-MuClidPreset root is host /
    // project state and goes straight to restoreStateFromTree.
    void loadPreset(const juce::File& file);

    // Commit a pre-built full preset into live state under suspend + rhythmsLock, then
    // finalise APVTS / mixer / global params. Called from loadPreset (stopped, immediate)
    // and from HotSwapStager::processSwaps (playing, at the loop boundary). `prepared`
    // is consumed (voices moved out).
    void commitStagedFullPreset(HotSwapStager::PreparedFullPreset& prepared);

    // JUCE AudioProcessor state (called from PluginProcessor's overrides).
    void getStateInformation(juce::MemoryBlock& destData);
    void restoreStateFromTree(const juce::ValueTree& state);
    void setStateInformation(const void* data, int sizeInBytes);

private:
    // Shared by the stopped (applyRhythmPreset) and playing (stageRhythmPreset) rhythm-preset loads.
    juce::ValueTree readRhythmPresetFile(const juce::File& file) const;
    PreparedRhythm  prepareRhythmPreset(const juce::File& file, int rhythmIndex, bool keepIdentity);
    HotSwapStager::PreparedFullPreset prepareFullPreset(const juce::ValueTree& root) const;

    // The .muClid tree (shared by preset files and the host session) and its atomic write.
    juce::ValueTree buildFullPresetTree(const juce::String& name, const juce::String& description,
                                        const juce::String& category, bool embedSamples, bool forSession);
    bool            writeFullPresetFile(const juce::File& file, const juce::String& name,
                                        const juce::String& description, const juce::String& category,
                                        bool embedSamples);
    // A session (a .muClid tree + every parameter it doesn't carry) restored at once.
    void            restoreSession(const juce::ValueTree& root);
    static void     applyPresetIdentity(const juce::ValueTree& state, Rhythm& r);

    PluginProcessor& proc_;

    // Helpers extracted from loadPreset / applyRhythmPreset / restoreStateFromTree
    // so each top-level function reads as a sequence of named steps. All run on
    // the message thread under a single mu_core::ScopedApvtsLoading guard managed
    // by the caller.



    // Restore the channel-strip params for slot `apvtsSlot`. Only relevant for
    // the .muClid format — legacy .muRhythm `ch_*` properties are intentionally
    // ignored by applyRhythmPreset (see Mixer-state-stays-with-slot policy).
    void restoreRhythmChannelParams(int apvtsSlot, const juce::ValueTree& rTree);

    // Restore embedded-sample bytes (preferred) or stored sample path for slot.
    // The three property-name args spell out exactly which keys to read so the
    // helper can serve unprefixed .muClid subtrees, "r0_"-prefixed .muRhythm
    // roots, and "r{i}_"-prefixed host-state roots from a single body. Fires
    // onLoadError if the linked sample is missing.
    void restoreRhythmSample(int slot, const juce::ValueTree& tree,
                              const juce::String& samplePathProp,
                              const juce::String& sampleDataProp,
                              const juce::String& sampleNameProp);


    // Restore the <GlobalState> child if present (mixer + FX algorithm params).
    void restoreGlobalState(const juce::ValueTree& root);
};

} // namespace mu_clid
