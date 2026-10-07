#include "Audio/SpinLock.h"   // mu-core: spin lock helpers
#include "Plugin/PluginProcessor.h"
#include "Modulation/MuOnModDest.h"           // isValidLaneDest
#include "Modulation/ModulatorSerialise.h"    // mu-core: modulator (de)serialise
#include "Persistence/PresetFiles.h"          // mu-core: shared preset-file handling

// mu-On preset I/O: full presets (.muOn), per-track presets (.muTrack), and the state
// capture / apply shared by the host session and full presets. Split out of
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

// The Rumble bar-volume envelope's curve points as a <RumbleEnv> node (read under its lock).
juce::ValueTree PluginProcessor::serialiseRumbleEnv()
{
    juce::ValueTree env("RumbleEnv");
    mu_core::spinLock(rumbleEnvLock);
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

// Restore the Rumble envelope from a <RumbleEnv> node (fewer than two points → unchanged).
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
    rumbleEnv.curvePoints = std::move(pts);
    mu_core::spinUnlock(rumbleEnvLock);
}

// The params + step grid + each lane's modulators + the Rumble envelope (session and full preset).
juce::ValueTree PluginProcessor::captureState()
{
    auto state = apvts.copyState();
    stepPattern.serialise(state);
    writeVoiceDataToState(state);
    state.removeChild(state.getChildWithName("RumbleEnv"), nullptr);
    state.addChild(serialiseRumbleEnv(), -1, nullptr);
    return state;
}

void PluginProcessor::applyStateTree(const juce::ValueTree& tree)
{
    apvts.replaceState(tree);
    stepPattern.deserialise(apvts.state);
    readVoiceDataFromState(apvts.state);
    restoreRumbleEnv(apvts.state.getChildWithName("RumbleEnv"));
    syncAllFxParams();   // re-seed mixer/FX (unchanged values skip listeners)
}

// While playing, stage a loaded full preset for the pattern's wrap (commitDeferredWork); while
// stopped, apply it now.
void PluginProcessor::useLoadedFullPreset(juce::ValueTree state)
{
    if (transportRunning.load(std::memory_order_relaxed))
        hotSwap.stageFull(std::move(state));
    else
    {
        for (int i = 0; i < kNumChannels; ++i) hotSwap.cancel(i);   // nothing staged may land on top
        applyStateTree(state);
    }
}

// A track preset: the lane's engine params, its step row (or the Rumble envelope) and its
// modulators. It belongs to one instrument, so it records which lane it came from.
void PluginProcessor::saveTrackPreset(int lane, const juce::String& name)
{
    if (lane < 0 || lane >= kNumChannels) return;
    auto dir = getPerSlotPresetDir();
    dir.createDirectory();

    juce::XmlElement root(kTrackPresetTag);
    root.setAttribute("lane", getChannelName(lane));
    mu_pp::writeLayerParams(root, *this, lanePrefix(lane));
    auto pattern = lane < kNumStepLanes ? stepPattern.serialiseTrack(lane) : serialiseRumbleEnv();
    if (auto xml = pattern.createXml()) root.addChildElement(xml.release());
    if (auto mods = mu_pp::serialiseModulators(voiceSlots[(size_t) lane]).createXml())
        root.addChildElement(mods.release());
    root.writeTo(dir.getChildFile(mu_pp::safePresetFileName(name, getChannelName(lane))
                                  + "." + getPerSlotPresetExtension()));
}

void PluginProcessor::loadTrackPreset(int lane, const juce::File& file)
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
    auto tree = juce::ValueTree::fromXml(*xml);
    if (transportRunning.load(std::memory_order_relaxed))
        hotSwap.stage(lane, std::move(tree));
    else
    {
        hotSwap.cancel(lane);
        applyTrackTree(lane, tree);
    }
}

// Apply a parsed track preset (the stopped load and the pattern-wrap commit), then tell the editor.
void PluginProcessor::applyTrackTree(int lane, const juce::ValueTree& tree)
{
    mu_pp::applyLayerParams(tree, apvts, lanePrefix(lane));
    if (lane < kNumStepLanes) stepPattern.deserialiseTrack(lane, tree.getChildWithName("Track"));
    else                      restoreRumbleEnv(tree.getChildWithName("RumbleEnv"));

    auto& slot = voiceSlots[(size_t) lane];
    mu_pp::clearModulators(slot);
    mu_pp::deserialiseModulators(tree.getChildWithName("Modulators"), slot, {},
                                 [lane](const std::string& id) { return isValidLaneDest(lane, id); });
    if (onTrackPresetLoaded) onTrackPresetLoaded(lane);
}

// Commit the hot-swaps that reached the pattern wrap: the full preset first (it supersedes the
// per-lane swaps), then each flagged lane.
void PluginProcessor::commitDeferredWork()
{
    if (hotSwap.consumeFull([this](juce::ValueTree& t) { applyStateTree(t); }) && onPresetSwapCommitted)
        onPresetSwapCommitted();
    for (int i = 0; i < kNumChannels; ++i)
        hotSwap.consume(i, [this, i](juce::ValueTree& t) { applyTrackTree(i, t); });
}

// The track presets saved from `lane` (a preset belongs to the instrument it was saved from).
juce::Array<juce::File> PluginProcessor::trackPresetFiles(int lane) const
{
    juce::Array<juce::File> files;
    for (const auto& f : mu_pp::listPresetFiles(getPerSlotPresetDir(), getPerSlotPresetExtension()))
        if (auto xml = juce::XmlDocument::parse(f))
            if (xml->hasTagName(kTrackPresetTag) && xml->getStringAttribute("lane") == getChannelName(lane))
                files.add(f);
    return files;
}

// Reset a lane: its engine params back to defaults and its modulators cleared.
void PluginProcessor::resetTrack(int lane)
{
    if (lane < 0 || lane >= kNumChannels) return;
    hotSwap.cancel(lane);   // a staged swap would re-fill what we're resetting
    mu_pp::applyLayerParams({}, apvts, lanePrefix(lane));
    mu_pp::clearModulators(voiceSlots[(size_t) lane]);
}

} // namespace mu_on
