#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Sequencer/GeneratorAliases.h"   // Rhythm

#include <functional>
#include <memory>
#include <optional>

class VoiceEngine;

// The Rhythm layer type's saved form, with no processor in it: a rhythm's .muRhythm / clip tree and its
// <Rhythm> node in a .muClid, the global <GlobalState> rows, and the way back (a node prepared into a
// Rhythm plus a primed VoiceEngine with its sample loaded). The host's preset code (PresetIO) decides
// when these run and installs the result; everything here is message thread.
namespace mu_clid
{

// What reading a rhythm's tree needs from the host: the content Samples folder that relative sample
// paths resolve against, the rate and block size the voice engine is primed at, and where load
// messages go (may be empty).
struct RhythmPresetContext
{
    juce::File samplesDir;
    double     sampleRate = 44100.0;
    int        blockSize  = 512;
    std::function<void(const juce::String&)> onLoadError;
};

// One rhythm prepared off the audio thread, ready to install into a slot.
struct PreparedRhythm
{
    Rhythm                       rhythm;
    std::unique_ptr<VoiceEngine> voice;   // null when the preset couldn't be read
    juce::String                 samplePath;
};

namespace rhythm_preset
{

// Per-channel mixer APVTS parameter suffixes (prefix "ch{i}_"), written onto a .muClid <Rhythm> node as ch_*.
inline const char* const kChannelSuffixes[] = {
    "lvl","pan","mute","solo","sendEff","sendDly","sendRev",
    "scSrc","scAmt","scAtk","scRel","outBus",
    nullptr
};

// True for a sample path inside the embedded-sample decode folder (%TEMP%/muClid_samples), which does
// not survive the OS session, so a save must embed the sample instead of writing the path.
bool isEmbeddedSampleTempPath(const juce::String& path);

// The rhythm's layer-preset tree (MuClidRhythm, r0_ properties) from the rhythm and its APVTS slot. A file
// (forFile) carries its preset metadata and the rhythm's name and colour; a clip carries neither, and
// always the sample path only, never the audio.
juce::ValueTree buildLayerTree(const Rhythm& r, const juce::AudioProcessorValueTreeState& apvts, int slot,
                               const juce::String& samplePath, const juce::File& samplesDir,
                               bool embedSample, bool forFile, const juce::String& presetName,
                               const juce::String& category, const juce::String& description);

// The rhythm's <Rhythm> node in a .muClid: its parameters, name, colour, sample, mixer strip, modulators
// and clip bank. A session (forSession) keeps temp-dir sample paths, a preset drops them.
juce::ValueTree buildFullPresetNode(const Rhythm& r, const juce::AudioProcessorValueTreeState& apvts, int slot,
                                    const juce::String& samplePath, const juce::File& samplesDir,
                                    bool embedSamples, bool forSession);

// The <GlobalState> child: every kGlobalParamDefs row in its v2 kinded form.
juce::ValueTree buildGlobalState(const juce::AudioProcessorValueTreeState& apvts);

// A rhythm built from a preset node: a .muRhythm root (prefix "r0_") or a .muClid <Rhythm> child (no
// prefix). Defaults for what the node lacks; `source` names it in load messages.
PreparedRhythm prepare(const juce::ValueTree& node, const juce::String& prefix,
                       const RhythmPresetContext& ctx, const juce::String& source);

// Writes a .muClid <Rhythm> node's ch_* strip rows into APVTS slot `slot`.
void restoreChannelStrip(juce::AudioProcessorValueTreeState& apvts, int slot, const juce::ValueTree& rTree);

// Writes a root's <GlobalState> into the APVTS (migrated; older files leave every global where it was).
void restoreGlobalState(juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& root);

// The legacy host-state sample restore for slot `slot`: embedded bytes (preferred) or the stored path,
// loaded into `voice`; the property names spell out which keys to read. The path the slot records, or
// nothing when the embedded data is unreadable (the slot is then left as it is).
std::optional<juce::String> restoreSample(VoiceEngine& voice, const juce::ValueTree& tree, int slot,
                                          const juce::String& samplePathProp, const juce::String& sampleDataProp,
                                          const juce::String& sampleNameProp, const RhythmPresetContext& ctx);

} // namespace rhythm_preset
} // namespace mu_clid
