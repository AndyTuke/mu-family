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
    // mu-tant modulation-destination validator (drops assignments to dests this
    // product doesn't expose; source IDs are ControlSequence ids, left unchecked).
    // Used only by the preset-load paths below.
    bool isValidModDest(const std::string& id)
    {
        for (int i = 0; i < kModDestCount; ++i)
            if (id == kModDestTable[i].id) return true;
        return false;
    }

    juce::String voicePrefix(int v) { return "v" + juce::String(v) + "_"; }

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
// with the full state (voiceColours) rather than the voice node.
void PluginProcessor::initVoiceState()
{
    juce::StringArray prefixes;
    for (int v = 0; v < kMaxVoices; ++v) prefixes.add(voicePrefix(v));
    initSlotState(prefixes);
}

// A voice's non-parameter data onto its node.
void PluginProcessor::writeVoiceExtras(int v, juce::ValueTree& node) const
{
    if (osc1UserPath[(size_t) v].isNotEmpty()) node.setProperty("o1WtPath", osc1UserPath[(size_t) v], nullptr);
    if (osc2UserPath[(size_t) v].isNotEmpty()) node.setProperty("o2WtPath", osc2UserPath[(size_t) v], nullptr);
    node.appendChild(mu_pp::serialiseModulators(voiceSlots[(size_t) v]),                 nullptr);
    node.appendChild(serialiseGate(gatePatterns[(size_t) v]),                 nullptr);
    node.appendChild(serialiseGate(filterPatterns[(size_t) v], "FilterGate"), nullptr);
    node.appendChild(serialiseGate(pitchPatterns[(size_t) v],  "PitchGate"),  nullptr);
}

void Pattern::writeExtras(juce::ValueTree& node) const { owner.ptr->writeVoiceExtras(owner.ptr->voiceOf(*this), node); }
void Pattern::applyExtras(const juce::ValueTree& node) { owner.ptr->applyVoiceExtras(owner.ptr->voiceOf(*this), node); }

// A voice's non-parameter data from its node. Absent children clear (an invalid tree empties a
// gate / the modulators); per-pattern spinlocks and modLock guard the audio thread, so no
// voicesLock is needed.
void PluginProcessor::applyVoiceExtras(int v, const juce::ValueTree& node)
{
    mu_pp::clearModulators(voiceSlots[(size_t) v]);
    mu_pp::deserialiseModulators(node.getChildWithName("Modulators"), voiceSlots[(size_t) v], {}, isValidModDest);
    refreshPitchQuantFlags(v);   // assignments changed → refresh stepped-pitch flags

    const int maxCells = maxSteps(std::numeric_limits<int>::max());   // 16 in demo
    deserialiseGate(node.getChildWithName("Gate"),       gatePatterns[(size_t) v],   maxCells);
    deserialiseGate(node.getChildWithName("FilterGate"), filterPatterns[(size_t) v], maxCells);
    deserialiseGate(node.getChildWithName("PitchGate"),  pitchPatterns[(size_t) v],  maxCells);

    resolveUserWavetable(node.getProperty("o1WtPath").toString(), osc1UserPath[(size_t) v], osc1UserIndex[(size_t) v]);
    resolveUserWavetable(node.getProperty("o2WtPath").toString(), osc2UserPath[(size_t) v], osc2UserIndex[(size_t) v]);
}

// A user wavetable path → its bank index. A missing file keeps the path (the UI shows "missing")
// but resolves to -1, so the oscillator falls back to its factory selection. Lock-free resolve
// first: a hot-swap stage pre-loaded the table, so the boundary commit never takes voicesLock
// (taking it would bail the audio render to silence for that block). Only a load that wasn't
// pre-loaded (stopped / host restore — audio silent then) takes the locked disk path.
void PluginProcessor::resolveUserWavetable(const juce::String& path, juce::String& storedPath, std::atomic<int>& index)
{
    storedPath = path;
    int idx = -1;
    if (path.isNotEmpty())
    {
        idx = bank.findByPath(path);
        if (idx < 0)
        {
            juce::File f(path);
            if (f.existsAsFile()) { const juce::ScopedLock sl(voicesLock); idx = bank.addOrLoadFile(f); }
        }
    }
    index.store(idx);
}

// Any saved state (host session or full preset, either format) in the composed shape, with each
// voice's old X-Mod params / assignments migrated.
juce::ValueTree PluginProcessor::toVoiceState(const juce::ValueTree& tree) const
{
    auto state = toComposedState(tree);
    auto slots = state.getChildWithName(mu_pp::kSlotsTag);
    for (int i = 0; i < slots.getNumChildren(); ++i)
    {
        auto node = slots.getChild(i);
        migrateXModVoice(node);
    }
    return state;
}

// A .muPattern: the voice's node (prefix-free param rows + its extras), so it loads into any voice.
void PluginProcessor::saveSlotPreset(int voice, const juce::String& name)
{
    if (voice < 0 || voice >= kMaxVoices) return;
    if (auto xml = captureSlotNode(voice, "MuTantVoice").createXml())
        mu_pp::writeXmlAtomically(*xml, getPerSlotPresetDir().getChildFile(mu_pp::safePresetFileName(name, "Voice")
                                                                           + "." + getPerSlotPresetExtension()),
                                  onLoadError);
}

void PluginProcessor::loadSlotPreset(int voice, const juce::File& file)
{
    if (voice < 0 || voice >= kMaxVoices || ! file.existsAsFile()) return;
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || ! xml->hasTagName("MuTantVoice"))
    {
        if (onLoadError) onLoadError("Could not read \"" + file.getFileName() + "\"");
        return;
    }
    auto tree = juce::ValueTree::fromXml(*xml);
    migrateXModVoice(tree);

    // Hot-swap: while playing, stage and commit at THIS voice's own loop boundary
    // (handleAsyncUpdate); while stopped, apply immediately. Referenced wavetables
    // are pre-loaded into the bank now so the boundary commit does no disk I/O.
    if (isInternalPlaying())
    {
        preloadWavetablesFromVoiceTree(tree);
        hotSwapStager.stageVoice(voice, std::move(tree));
    }
    else
    {
        applyVoicePresetTree(voice, tree);
    }
}

// Apply a voice node (the stopped load and the boundary commit). A parameter the node lacks goes
// back to its default, as in every product.
void PluginProcessor::applyVoicePresetTree(int voice, const juce::ValueTree& tree)
{
    if (voice < 0 || voice >= kMaxVoices) return;
    applySlotNode(voice, tree);
}

// Warm the wavetable bank (dedup-by-path, append under voicesLock) for the user
// wavetables a staged voice tree references, so the boundary commit is disk-free.
void PluginProcessor::preloadWavetablesFromVoiceTree(const juce::ValueTree& voiceTree)
{
    auto warm = [this](const juce::String& path)
    {
        if (path.isEmpty()) return;
        if (bank.findByPath(path) >= 0) return;       // already in the bank — nothing to do
        juce::File f(path);
        if (! f.existsAsFile()) return;
        // Decode (file read + WAV decode + FFT mip build) OFF the lock — this is the
        // slow part. Only the brief append takes voicesLock, so the audio render is
        // excluded for microseconds, not for the whole decode (the residual swap
        // pause: decoding under the lock silenced the render for the decode duration).
        auto wt = bank.decodeFile(f);
        if (wt.frames <= 0) return;
        const juce::ScopedLock sl(voicesLock);
        if (bank.findByPath(path) < 0) bank.appendTable(std::move(wt));   // defensive re-check
    };
    warm(voiceTree.getProperty("o1WtPath").toString());
    warm(voiceTree.getProperty("o2WtPath").toString());
}

// Same, for a full preset: each voice's <Slot> carries o1WtPath / o2WtPath.
void PluginProcessor::preloadWavetablesFromState(const juce::ValueTree& state)
{
    const auto slots = state.getChildWithName(mu_pp::kSlotsTag);
    for (int i = 0; i < slots.getNumChildren(); ++i)
        preloadWavetablesFromVoiceTree(slots.getChild(i));
}

juce::String PluginProcessor::serialiseVoiceColours() const
{
    juce::String s;
    for (int i = 0; i < kMaxVoices; ++i)
        s += (i ? "," : "") + juce::String(voiceColourIndex[(size_t) i]);
    return s;
}

void PluginProcessor::restoreVoiceColours(const juce::String& csv)
{
    if (csv.isEmpty()) return;
    const auto toks = juce::StringArray::fromTokens(csv, ",", "");
    for (int i = 0; i < kMaxVoices && i < toks.size(); ++i)
        voiceColourIndex[(size_t) i] = juce::jlimit(0, kMaxVoices - 1, toks[i].getIntValue());
}

juce::File PluginProcessor::getPerSlotPresetDir() const { return getContentDir().getChildFile("Voices"); }
juce::File PluginProcessor::getWavetablesDir()  const { return getContentDir().getChildFile("Wavetables"); }

// A full preset / host session: the composed state (globals + every voice's node) with the voice
// count and colours. Built fresh, so a host save never touches the live tree.
juce::ValueTree PluginProcessor::captureFullPreset()
{
    auto state = captureComposedState();
    state.setProperty("numVoices", numVoices.load(), nullptr);
    state.setProperty("voiceColours", serialiseVoiceColours(), nullptr);
    return state;
}

// Hot-swap: while the transport is playing, stage the parsed state and commit it at voice 0's
// next loop boundary (handleAsyncUpdate) so the switch is musically seamless; while stopped,
// apply immediately. Converted (and migrated) here, and the wavetables it references pre-loaded
// into the bank, so the boundary commit does no parsing or disk I/O.
void PluginProcessor::useLoadedFullPreset(juce::ValueTree state)
{
    auto composed = toVoiceState(state);
    if (isInternalPlaying())
    {
        preloadWavetablesFromState(composed);
        hotSwapStager.stageFull(std::move(composed));
    }
    else
    {
        applyFullPresetTree(composed);
    }
}

// Apply a composed full state immediately (the shared commit path for the stopped load, the
// boundary commit and host state restore).
void PluginProcessor::applyFullPresetTree(const juce::ValueTree& state)
{
    // No blanket voicesLock here: every structure this touches is already guarded by
    // its own fine-grained lock that the audio render respects — APVTS params are
    // atomic, gate patterns use editLock (deserialiseGate), modulator slots use modLock
    // (deserialise/clearModulators), and the wavetable bank append self-locks. Holding a
    // blanket lock instead made the audio render bail to silence AND froze the transport
    // for the whole commit (the hot-swap glitch). Without it, a concurrent block sees
    // old-or-new per structure (≤1 block, inaudible) instead.

    // Demo cap: an unlicensed build activates at most demoMaxChannels() voices. The other
    // voices' params/data still load but stay inactive (getNumChannels() == numVoices).
    const int oldN = numVoices.load(std::memory_order_relaxed);
    int nv = juce::jlimit(1, kMaxVoices, (int) state.getProperty("numVoices", 1));
    if (! isLicensed())
        nv = juce::jmin(nv, demoMaxChannels());

    // Retire-tail: when this preset drops voices while playing, snapshot each dropped
    // voice's OLD engine + insert config NOW (before the new params land)
    // and arm a short fade so its comb-filter / insert tail rings out.
    if (isInternalPlaying() && nv < oldN)
    {
        const int ramp = retireRampSamples();
        for (int v = nv; v < oldN; ++v)
        {
            auto& r = retiring[(size_t) v];
            r.samplesLeft.store(0, std::memory_order_release);   // pause any in-flight read of `config`
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

    applyComposedState(state);
    numVoices.store(nv);
    restoreVoiceColours(state.getProperty("voiceColours", "").toString());
    syncAllFxParams();   // re-seed mixer/FX engine state (unchanged values skip listeners)
}

} // namespace mu_tant
