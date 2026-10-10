#include "Audio/SpinLock.h"   // mu-core: spin lock helpers
#include "Plugin/PluginProcessor.h"
#include "Modulation/MuOnModDest.h"           // isValidLaneDest
#include "Modulation/ModulatorSerialise.h"    // mu-core: modulator (de)serialise
#include "Persistence/PresetFiles.h"          // mu-core: shared preset-file handling

// mu-On preset I/O on the family composed state (mu-core LayerState): full presets (.muOn),
// per-track presets (.muTrack) and host sessions all build / apply the same lane node. Split out of
// PluginProcessor.cpp to mirror mu-Tant's PluginProcessor_Preset.cpp.

namespace mu_on
{

namespace
{
    constexpr const char* kTrackPresetTag = "MuOnTrack";

    // Each lane's engine params share a prefix; a track preset holds the lane's set.
    constexpr const char* kLanePrefix[kNumChannels] = { "k_", "b_", "h_", "s_", "r_" };
}

juce::String PluginProcessor::lanePrefix(int lane)
{
    return kLanePrefix[(size_t) juce::jlimit(0, kNumChannels - 1, lane)];
}

// The Rumble volume envelope — its length (note value × multiplier, as a modulator's Loop) and
// curve points — as a <RumbleEnv> node (read under its lock).
juce::ValueTree PluginProcessor::serialiseRumbleEnv()
{
    juce::ValueTree env("RumbleEnv");
    mu_core::spinLock(rumbleEnvLock);
    env.setProperty("loopNV",   mu_pp::enumName(mu_audio::kNoteValueNames, (int) rumbleEnv.loopNoteValue), nullptr);
    env.setProperty("loopMod",  mu_pp::enumName(mu_audio::kNoteModNames,   (int) rumbleEnv.loopNoteMod),   nullptr);
    env.setProperty("loopMult", rumbleEnv.loopMultiplier, nullptr);
    for (const auto& p : rumbleEnv.curvePoints)
    {
        juce::ValueTree pt("P");
        pt.setProperty("x", p.x, nullptr);
        pt.setProperty("y", p.y, nullptr);
        env.addChild(pt, -1, nullptr);
    }
    mu_core::spinUnlock(rumbleEnvLock);
    return env;
}

// Restore the Rumble envelope from a <RumbleEnv> node (fewer than two points → unchanged). A node
// saved before the envelope had a length is one bar, as it always was.
void PluginProcessor::restoreRumbleEnv(const juce::ValueTree& env)
{
    std::vector<ControlSequence::CurvePoint> pts;
    for (int i = 0; i < env.getNumChildren(); ++i)
    {
        const auto p = env.getChild(i);
        ControlSequence::CurvePoint cp;
        cp.x = (float) p.getProperty("x", 0.0f);
        cp.y = (float) p.getProperty("y", 0.0f);
        pts.push_back(cp);
    }
    if (pts.size() < 2) return;
    mu_core::spinLock(rumbleEnvLock);
    rumbleEnv.curvePoints    = std::move(pts);
    rumbleEnv.loopNoteValue  = (NoteValue) mu_pp::readEnumIndex(env, "loopNV",  mu_audio::kNoteValueNames, (int) NoteValue::Quarter);
    rumbleEnv.loopNoteMod    = (NoteMod)   mu_pp::readEnumIndex(env, "loopMod", mu_audio::kNoteModNames,   (int) NoteMod::None);
    rumbleEnv.loopMultiplier = juce::jmax(1, (int) env.getProperty("loopMult", 4));
    mu_core::spinUnlock(rumbleEnvLock);
}

// Describe the lanes once (ctor): each lane's engine params, its step row (the Rumble lane: its
// bar-volume envelope) and its modulators. Every track preset, full preset and host session is
// built and applied from this.
void PluginProcessor::initLaneState()
{
    juce::StringArray prefixes;
    for (int l = 0; l < kNumChannels; ++l) prefixes.add(lanePrefix(l));
    initSlotState(prefixes,
        { [this](int l, juce::ValueTree& node)
          {
              node.appendChild(l < kNumStepLanes ? stepPattern.serialiseTrack(l) : serialiseRumbleEnv(), nullptr);
              node.appendChild(mu_pp::serialiseModulators(voiceSlots[(size_t) l]), nullptr);
          },
          [this](int l, const juce::ValueTree& node)
          {
              // An absent <Track> clears the lane's steps; an absent envelope keeps the current one.
              if (l < kNumStepLanes) stepPattern.deserialiseTrack(l, node.getChildWithName("Track"));
              else                   restoreRumbleEnv(node.getChildWithName("RumbleEnv"));

              auto& slot = voiceSlots[(size_t) l];
              mu_pp::clearModulators(slot);
              mu_pp::deserialiseModulators(node.getChildWithName("Modulators"), slot, {},
                                           [l](const std::string& id) { return isValidLaneDest(l, id); });
          } });
}

// Any saved state (host session or full preset, either format) in the composed shape. Older
// states kept the grid as one <Pattern> and the envelope at the root (each moves into its lane),
// and the oldest called each lane's modulator node <Lane>.
juce::ValueTree PluginProcessor::toLaneState(const juce::ValueTree& tree) const
{
    juce::ValueTree source = tree;
    if (auto vd = tree.getChildWithName(mu_pp::kChannelDataTag); vd.isValid() && vd.getChildWithName("Lane").isValid())
    {
        source = tree.createCopy();
        auto data = source.getChildWithName(mu_pp::kChannelDataTag);
        for (int i = 0; i < data.getNumChildren(); ++i)
            if (data.getChild(i).hasType("Lane"))
            {
                juce::ValueTree node(mu_pp::kChannelNodeTag);
                node.copyPropertiesAndChildrenFrom(data.getChild(i), nullptr);
                data.removeChild(i, nullptr);
                data.addChild(node, i, nullptr);
            }
    }

    return toComposedState(source, [](const juce::ValueTree& child, juce::ValueTree& composed)
    {
        if (child.hasType("Pattern"))
        {
            for (int i = 0; i < child.getNumChildren(); ++i)
                if (const auto row = child.getChild(i); row.hasType("Track"))
                    if (auto node = mu_pp::findSlotNode(composed, (int) row.getProperty("i", -1)); node.isValid())
                        node.appendChild(row.createCopy(), nullptr);
        }
        else if (child.hasType("RumbleEnv"))
        {
            if (auto node = mu_pp::findSlotNode(composed, kNumStepLanes); node.isValid())
                node.appendChild(child.createCopy(), nullptr);
        }
    });
}

// Apply a composed state (host restore, full-preset load or its pattern-wrap commit), then
// re-seed the mixer / FX engines.
void PluginProcessor::applyStateTree(const juce::ValueTree& state)
{
    applyComposedState(state);
    syncAllFxParams();   // re-seed mixer/FX (unchanged values skip listeners)
}

// While playing, stage a loaded full preset for the pattern's wrap (commitDeferredWork); while
// stopped, apply it now. Converted to the composed shape here, so the commit does no parsing.
void PluginProcessor::useLoadedFullPreset(juce::ValueTree state)
{
    hotSwap.useFull(toLaneState(state));
}

// A track preset: the lane's node (engine param rows, step row or envelope, modulators). It
// belongs to one instrument, so it records which lane it came from.
void PluginProcessor::saveSlotPreset(int lane, const juce::String& name)
{
    if (lane < 0 || lane >= kNumChannels) return;
    auto node = captureSlotNode(lane, kTrackPresetTag);
    node.setProperty("lane", getChannelName(lane), nullptr);
    if (auto xml = node.createXml())
        mu_pp::writeXmlAtomically(*xml, getPerSlotPresetDir().getChildFile(mu_pp::safePresetFileName(name, getChannelName(lane))
                                                                           + "." + getPerSlotPresetExtension()),
                                  onLoadError);
}

void PluginProcessor::loadSlotPreset(int lane, const juce::File& file)
{
    if (lane < 0 || lane >= kNumChannels || ! file.existsAsFile()) return;
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || ! xml->hasTagName(kTrackPresetTag))
    {
        if (onLoadError) onLoadError("Could not read \"" + file.getFileName() + "\"");
        return;
    }
    if (xml->getStringAttribute("lane") != getChannelName(lane))
    {
        if (onLoadError) onLoadError("\"" + file.getFileNameWithoutExtension() + "\" is a "
                                     + xml->getStringAttribute("lane") + " preset");
        return;
    }

    // While playing, stage it for the pattern's wrap (commitDeferredWork); while stopped, apply now.
    hotSwap.useSlot(lane, juce::ValueTree::fromXml(*xml));
}

// Apply a lane node (the stopped load and the pattern-wrap commit), then tell the editor.
void PluginProcessor::applyTrackTree(int lane, const juce::ValueTree& tree)
{
    applySlotNode(lane, tree);
    if (onSlotPresetCommitted) onSlotPresetCommitted(lane);
}

// Commit the hot-swaps that reached the pattern wrap: the full preset first (it supersedes the
// per-lane swaps), then each flagged lane.
void PluginProcessor::commitDeferredWork()
{
    if (hotSwap.commit() && onPresetSwapCommitted)
        onPresetSwapCommitted();
}

// The track presets saved from `lane` (a preset belongs to the instrument it was saved from).
juce::Array<juce::File> PluginProcessor::slotPresetFiles(int lane) const
{
    juce::Array<juce::File> files;
    for (const auto& f : mu_pp::listPresetFiles(getPerSlotPresetDir(), getPerSlotPresetExtension()))
    {
        const auto meta = mu_pp::readPresetMeta(f);
        if (meta.rootTag == kTrackPresetTag && meta.attribute("lane") == getChannelName(lane))
            files.add(f);
    }
    return files;
}

// Reset a lane: its engine params back to defaults and its modulators cleared. The step row
// and the Rumble envelope are kept — a reset is a sound reset, not a pattern wipe.
void PluginProcessor::resetSlot(int lane)
{
    if (lane < 0 || lane >= kNumChannels) return;
    hotSwap.cancel(lane);   // a staged swap would re-fill what we're resetting
    slotLayout.applyParams({}, lane);
    mu_pp::clearModulators(voiceSlots[(size_t) lane]);
}

} // namespace mu_on
