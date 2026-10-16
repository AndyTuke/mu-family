#include "Plugin/PluginProcessor.h"
#include "Audio/Scales.h"
#include "Modulation/MuTantModDest.h"
#include "Modulation/ModulatorSerialise.h"   // mu-core: shared modulator (de)serialise
#include "Sequencer/GatePatternSerialise.h"  // mu-tant: gate (de)serialise
#include "Persistence/PresetFiles.h"         // mu-core: shared preset-file handling
#include "Persistence/LayerState.h"           // mu-core: composed slot state

#include <thread>

// Preset / per-voice I/O on the family composed state (mu-core LayerState): a voice is one node —
// its v{N}_ param rows, user wavetable paths, three gate patterns and modulators — written and
// applied by the same code for a .muPattern, a .muTant and a host session. Plus voice colours and
// the best-effort X-Mod preset migration. Split out of PluginProcessor.cpp
// (which kept ctor / processBlock / modulation / voice management) to keep that TU
// under the family god-file threshold, mirroring mu-clid's PresetIO.cpp split.
// These are all PluginProcessor:: member definitions — same class, separate TU.

namespace mu_tant
{

namespace
{
    // ── X-Mod preset migration (best-effort) ─────────────────────────────────
    // Old presets / patches carry xmod_fm / _am / _ring (+ modulator assignments to
    // xmod.fm/.am/.ring); the 2-lane redesign replaces them. Old FM depth → Lane A index in
    // PM mode (the old "FM" was phase-mod); old AM / Ring → Lane B depth in the matching amp
    // mode (the larger wins if both were set); modulator dest ids remapped. A no-op on a voice
    // that already has the new params.

    // Rewrite any "dest" property naming an old xmod id → its new id (recursive).
    void migrateModDestIds(juce::ValueTree tree)
    {
        if (! tree.isValid()) return;
        if (tree.hasProperty("dest"))
        {
            const juce::String d = tree.getProperty("dest").toString();
            if      (d == "xmod.fm")                       tree.setProperty("dest", "xmod.index", nullptr);
            else if (d == "xmod.am" || d == "xmod.ring")   tree.setProperty("dest", "xmod.depth", nullptr);
        }
        for (int i = 0; i < tree.getNumChildren(); ++i)
            migrateModDestIds(tree.getChild(i));
    }

    // An old X-Mod row's value on its 0..100 range: actual (x, from an old full state's PARAM)
    // or normalised (v, from an old .muPattern).
    float oldXmodValue(const juce::ValueTree& row)
    {
        return row.hasProperty("x") ? (float) (double) row.getProperty("x")
                                    : (float) (double) row.getProperty("v") * 100.0f;
    }

    void setActualRow(juce::ValueTree& node, const juce::String& id, float actual)
    {
        auto row = node.getChildWithProperty("id", id);
        if (! row.isValid())
        {
            row = juce::ValueTree(mu_pp::kRowTag);
            row.setProperty("id", id, nullptr);
            node.appendChild(row, nullptr);
        }
        row.removeProperty("v", nullptr);
        row.removeProperty("c", nullptr);
        row.setProperty("x", actual, nullptr);
    }

    // One voice node (a .muPattern, or a state's <Slot>) migrated in place.
    void migrateXModVoice(juce::ValueTree& node)
    {
        const auto fm = node.getChildWithProperty("id", "xmod_fm");
        if (fm.isValid() && ! node.getChildWithProperty("id", "xmod_index").isValid())
        {
            const auto am = node.getChildWithProperty("id", "xmod_am");
            const auto rg = node.getChildWithProperty("id", "xmod_ring");
            const float amv = am.isValid() ? oldXmodValue(am) : 0.0f;
            const float rgv = rg.isValid() ? oldXmodValue(rg) : 0.0f;
            const bool amWins = amv >= rgv;
            setActualRow(node, "xmod_index",     oldXmodValue(fm));        // FM depth → index
            setActualRow(node, "xmod_phaseMode", 1.0f);                    // PM
            setActualRow(node, "xmod_depth",     amWins ? amv : rgv);
            setActualRow(node, "xmod_ampMode",   amWins ? 0.0f : 1.0f);    // AM : RM
        }
        migrateModDestIds(node.getChildWithName("Modulators"));
    }
}

// Describe the voices once (ctor): each voice's v{N}_ param prefix; its user wavetable paths, gate /
// filter / pitch envelopes and modulators save through Pattern.
// Every .muPattern, .muTant and host session builds and applies a voice from this. A voice's colour is its slot's identity, not part of its sound, so it stays
// with the full state (the base saves it) rather than the voice node.
void PluginProcessor::initVoiceState()
{
    juce::StringArray prefixes;
    for (int v = 0; v < mu_limits::kMaxLayers; ++v) prefixes.add(layerParamPrefix(v));
    initLayerState(prefixes);
}

// A saved user wavetable path → its bank index (-1: none, or the file is gone). Lock-free resolve first:
// a hot-swap stage pre-loaded the table, so the boundary commit never takes bankLock (taking it would bail
// the audio render to silence for that block). Only a load that wasn't pre-loaded (stopped / host restore —
// audio silent then) takes the locked disk path.
int PluginProcessor::resolvePatternWavetable(const juce::String& path)
{
    int idx = -1;
    if (path.isNotEmpty())
    {
        idx = bank.findByPath(path);
        if (idx < 0)
        {
            juce::File f(path);
            if (f.existsAsFile()) idx = bankIndexFor(f);
        }
    }
    return idx;
}

// Any saved state (host session or full preset, either format) in the composed shape, with each
// voice's old X-Mod params / assignments migrated.
juce::ValueTree PluginProcessor::toVoiceState(const juce::ValueTree& tree) const
{
    auto state = toComposedState(tree);
    auto slots = state.getChildWithName(mu_pp::kLayersTag);
    for (int i = 0; i < slots.getNumChildren(); ++i)
    {
        auto node = slots.getChild(i);
        migrateXModVoice(node);
    }
    return state;
}

// A .muPattern: the voice's node (prefix-free param rows + its extras), so it loads into any voice.
juce::ValueTree PluginProcessor::captureLayerPresetTree(int voice)
{
    if (voice < 0 || voice >= mu_limits::kMaxLayers) return {};
    return captureLayerNode(voice, "MuTantVoice");
}

void PluginProcessor::saveLayerPreset(int voice, const juce::String& name)
{
    if (auto xml = captureLayerPresetTree(voice).createXml())
        mu_pp::writeXmlAtomically(*xml, getLayerPresetDir().getChildFile(mu_pp::safePresetFileName(name, "Voice")
                                                                           + "." + getLayerPresetExtension()),
                                  onLoadError);
}

void PluginProcessor::loadLayerPreset(int voice, const juce::File& file)
{
    if (voice < 0 || voice >= mu_limits::kMaxLayers || ! file.existsAsFile()) return;
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || ! xml->hasTagName("MuTantVoice"))
    {
        if (onLoadError) onLoadError("Could not read \"" + file.getFileName() + "\"");
        return;
    }
    stageLayerPreset(voice, juce::ValueTree::fromXml(*xml), false, false);
}

// Everything a voice-preset load does after the parse (file or clip): migrate, pre-load, stage.
void PluginProcessor::stageLayerPresetTree(int voice, juce::ValueTree tree, bool keepIdentity)
{
    juce::ignoreUnused(keepIdentity);   // a voice node carries no name or colour
    if (voice < 0 || voice >= mu_limits::kMaxLayers || ! tree.isValid()) return;
    migrateXModVoice(tree);

    // Hot-swap: while the resolved transport plays (host, MIDI clock or internal), stage and commit
    // at THIS voice's own loop boundary (runDeferredWork); while stopped, apply immediately. The
    // swapper makes that call. Referenced wavetables are pre-loaded into the bank whatever the play
    // state is now (dedup-by-path makes a repeat free), because the transport can start between
    // this and the staging, and a boundary commit must never do disk I/O.
    preloadWavetablesFromVoiceTree(tree);
    layerSwapper.useSlot(voice, std::move(tree));
}

// Apply a voice node (the stopped load and the boundary commit). A parameter the node lacks goes
// back to its default, as in every product.
void PluginProcessor::applyVoicePresetTree(int voice, const juce::ValueTree& tree)
{
    if (voice < 0 || voice >= mu_limits::kMaxLayers) return;
    applyLayerNode(voice, tree);
}

// Warm the wavetable bank (dedup-by-path, append under bankLock) for the user
// wavetables a staged voice tree references, so the boundary commit is disk-free.
void PluginProcessor::preloadWavetablesFromVoiceTree(const juce::ValueTree& voiceTree)
{
    auto warm = [this](const juce::String& path)
    {
        if (path.isEmpty()) return;
        juce::File f(path);
        if (! f.existsAsFile()) return;
        bankIndexFor(f);   // decodes off the lock, appends under it for microseconds
    };
    warm(voiceTree.getProperty("o1WtPath").toString());
    warm(voiceTree.getProperty("o2WtPath").toString());
}

// Same, for a full preset: each voice's <Slot> carries o1WtPath / o2WtPath.
void PluginProcessor::preloadWavetablesFromState(const juce::ValueTree& state)
{
    const auto slots = state.getChildWithName(mu_pp::kLayersTag);
    for (int i = 0; i < slots.getNumChildren(); ++i)
        preloadWavetablesFromVoiceTree(slots.getChild(i));
}

juce::File PluginProcessor::getLayerPresetDir() const { return getContentDir().getChildFile("Voices"); }
juce::File PluginProcessor::getWavetablesDir()  const { return getContentDir().getChildFile("Wavetables"); }

// A full preset / host session: the composed state (globals + every voice's node) with the voice
// count and colours. Built fresh, so a host save never touches the live tree.
juce::ValueTree PluginProcessor::captureFullPreset()
{
    return captureComposedState();
}

// Hot-swap: while the resolved transport is playing, stage the parsed state and commit it at the
// next loop boundary (runDeferredWork) so the switch is musically seamless; while stopped,
// apply immediately. Converted (and migrated) here, and the wavetables it references pre-loaded
// into the bank, so the boundary commit does no parsing or disk I/O.
void PluginProcessor::useLoadedFullPreset(juce::ValueTree state)
{
    auto composed = toVoiceState(state);
    // Pre-load whatever the play state: the transport may start before the swapper stages it.
    preloadWavetablesFromState(composed);
    layerSwapper.useFull(std::move(composed));
}

// Apply a composed full state immediately (the shared commit path for the stopped load, the
// boundary commit and host state restore).
void PluginProcessor::applyFullPresetTree(const juce::ValueTree& state)
{
    // No blanket layersLock here: every structure this touches is already guarded by
    // its own fine-grained lock that the audio render respects — APVTS params are
    // atomic, gate patterns use editLock (deserialiseGate), modulator slots use modLock
    // (deserialise/clearModulators), and the wavetable bank append self-locks. Holding a
    // blanket lock instead made the audio render bail to silence AND froze the transport
    // for the whole commit (the hot-swap glitch). Without it, a concurrent block sees
    // old-or-new per structure (≤1 block, inaudible) instead.

    // Message thread only, which serialises it against every structural edit (add / remove / swap /
    // reset): hosts restore state on their main thread (VST3 IComponent::setState, the CLAP state
    // extension's load, the standalone's own message loop), and the staged commit runs there too.
    // That is also why the retire-tail setup below may arm retiring[] without layersLock.
    const int oldN = numLayers.load(std::memory_order_relaxed);
    const int nv = savedLayerCount(state);

    // Retire-tail: when this preset drops voices while playing, snapshot each dropped
    // voice's OLD engine + insert config NOW (before the new params land)
    // and arm a short fade so its comb-filter / insert tail rings out.
    if (layerSwapper.isRunning() && nv < oldN)
    {
        const int ramp = retireRampSamples();
        for (int v = nv; v < oldN; ++v)
        {
            auto& r = retiring[(size_t) v];
            // A tail still ringing is being read by the audio thread, so its config must not be
            // rewritten; back-to-back swaps that drop the same voice miss the second tail by design.
            if (r.samplesLeft.load(std::memory_order_acquire) > 0) continue;
            r.config   = readConfig(v);                          // OLD osc + filter
            const auto& vp = voicePtrs[(size_t) v];
            r.insAlgo  = (int) vp.drvChar->load();
            r.insP     = { juce::jlimit(0.0f, 1.0f, vp.insP1->load()),
                           juce::jlimit(0.0f, 1.0f, vp.insP2->load()),
                           juce::jlimit(0.0f, 1.0f, vp.insP3->load()),
                           juce::jlimit(0.0f, 1.0f, vp.insP4->load()) };
            r.gainStep = 1.0f / (float) ramp;
            r.samplesLeft.store(ramp, std::memory_order_release);
        }
    }

    applyComposedState(state);   // also stores the count and restores the colours
    syncAllFxParams();   // re-seed mixer/FX engine state (unchanged values skip listeners)
}

} // namespace mu_tant
