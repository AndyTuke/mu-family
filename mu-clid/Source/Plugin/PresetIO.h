#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Plugin/HotSwapStager.h"   // HotSwapStager::PreparedFullPreset (commit payload)
#include "Persistence/RhythmPresetTree.h"   // the Rhythm type's saved form (PreparedRhythm, RhythmPresetContext)

namespace mu_clid {

class PluginProcessor;

// mu-Clid's preset orchestration: when a rhythm preset, a full preset or a host session is read or
// written, and how the result is installed into the processor (the stager, the slot engines, the
// locks). The rhythm's own saved form is Persistence/RhythmPresetTree. Declared as friend of
// PluginProcessor so it can reach its private members via proc; PluginProcessor keeps thin public
// delegates to the methods below.
class PresetIO
{
public:
    explicit PresetIO(PluginProcessor& proc) : proc(proc) {}

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

    // Layer-preset tree hooks behind ProcessorBase's clip API. The capture is the rhythm as a layer
    // preset holds it minus file metadata, name and colour, with the sample by path only; the stage is
    // everything a .muRhythm load does after the parse (installs at once when stopped, else commits
    // at the rhythm's boundary). Callers go through ProcessorBase::stageLayerPreset, not this directly.
    juce::ValueTree captureRhythmPresetTree(int rhythmIndex);
    void stageRhythmPresetTree(int rhythmIndex, const juce::ValueTree& state, bool keepIdentity);
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
    // Returns false when the file could not be read (nothing was loaded or staged).
    bool loadPreset(const juce::File& file);

    // Commit a pre-built full preset into live state under suspend + layersLock, then
    // finalise APVTS / mixer / global params. Called from loadPreset (stopped, immediate)
    // and from HotSwapStager::processSwaps (playing, at the loop boundary). `prepared`
    // is consumed (voices moved out).
    void commitStagedFullPreset(HotSwapStager::PreparedFullPreset& prepared, int originEff = 0,
                                uint32_t frameEpoch = RhythmCursor::kAnyFrameEpoch);

    // JUCE AudioProcessor state (called from PluginProcessor's overrides).
    void getStateInformation(juce::MemoryBlock& destData);
    void restoreStateFromTree(const juce::ValueTree& state);
    void setStateInformation(const void* data, int sizeInBytes);

private:
    // Shared by the stopped (applyRhythmPreset) and playing (stageRhythmPreset) rhythm-preset loads.
    juce::ValueTree readRhythmPresetFile(const juce::File& file) const;
    PreparedRhythm  prepareRhythmPresetTree(const juce::ValueTree& state, int rhythmIndex, bool keepIdentity,
                                            const juce::String& source);
    bool            applyRhythmPresetTree(const juce::ValueTree& state, int targetIndex, bool keepIdentity,
                                          const juce::String& source);
    // The .muRhythm tree: a file (forFile) adds its metadata, name and colour; a clip does not.
    juce::ValueTree buildRhythmPresetTree(int rhythmIndex, bool embedSample, bool forFile,
                                          const juce::String& presetName, const juce::String& category,
                                          const juce::String& description);
    HotSwapStager::PreparedFullPreset prepareFullPreset(const juce::ValueTree& root) const;

    // The .muClid tree (shared by preset files and the host session) and its atomic write.
    juce::ValueTree buildFullPresetTree(const juce::String& name, const juce::String& description,
                                        const juce::String& category, bool embedSamples, bool forSession);
    bool            writeFullPresetFile(const juce::File& file, const juce::String& name,
                                        const juce::String& description, const juce::String& category,
                                        bool embedSamples);
    // A session (a .muClid tree + every parameter it doesn't carry) restored at once.
    void            restoreSession(const juce::ValueTree& root);
    // The context a rhythm tree is read in: the Samples folder, the prepared rate and block size, onLoadError.
    RhythmPresetContext presetContext() const;

    PluginProcessor& proc;

    // The file name of the .muRhythm being staged, so load messages can name it; empty for a clip.
    juce::String loadSource;

    // Restore embedded-sample bytes (preferred) or stored sample path for slot.
    // The three property-name args spell out exactly which keys to read so the
    // helper can serve unprefixed .muClid subtrees, "r0_"-prefixed .muRhythm
    // roots, and "r{i}_"-prefixed host-state roots from a single body. Fires
    // onLoadError if the linked sample is missing.
    void restoreRhythmSample(int slot, const juce::ValueTree& tree,
                              const juce::String& samplePathProp,
                              const juce::String& sampleDataProp,
                              const juce::String& sampleNameProp);
};

} // namespace mu_clid
