#include "Plugin/PluginProcessor.h"
#include "Audio/Scales.h"
#include "Modulation/MuTantModDest.h"
#include "Modulation/ModulatorSerialise.h"   // mu-core: shared modulator (de)serialise
#include "Sequencer/GatePatternSerialise.h"  // mu-tant: gate (de)serialise

#include <thread>

// Preset / per-voice I/O, voice-colour + <VoiceData> serialisation, and the
// best-effort X-Mod preset migration for mu-tant. Split out of PluginProcessor.cpp
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
}

void PluginProcessor::saveVoicePreset(int voice, const juce::String& name)
{
    auto dir = getPerSlotPresetDir();
    dir.createDirectory();
    juce::String safe = name.replaceCharacters("\\/:|*?<>\"", "_________");
    if (safe.isEmpty()) safe = "Voice";

    const juce::String prefix = juce::String("v") + juce::String(voice) + "_";
    juce::XmlElement root("MuTantVoice");
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
        {
            const juce::String id = rp->getParameterID();
            if (id.startsWith(prefix))
            {
                auto* e = root.createNewChildElement("p");
                e->setAttribute("id", id.substring(prefix.length()));  // voice-agnostic base
                e->setAttribute("v", (double) rp->getValue());         // normalised 0..1
            }
        }

    // User wavetable selections (paths) — carried alongside the params.
    if (osc1UserPath[(size_t) voice].isNotEmpty()) root.setAttribute("o1WtPath", osc1UserPath[(size_t) voice]);
    if (osc2UserPath[(size_t) voice].isNotEmpty()) root.setAttribute("o2WtPath", osc2UserPath[(size_t) voice]);

    // Modulators + gate + filter gate live outside APVTS — serialise them alongside
    // the params so a .muPattern carries the whole voice.
    if (auto mods = mu_pp::serialiseModulators(voiceSlots[(size_t) voice]).createXml())
        root.addChildElement(mods.release());
    if (auto gate = serialiseGate(gatePatterns[(size_t) voice]).createXml())
        root.addChildElement(gate.release());
    if (auto fGate = serialiseGate(filterPatterns[(size_t) voice], "FilterGate").createXml())
        root.addChildElement(fGate.release());
    if (auto pGate = serialiseGate(pitchPatterns[(size_t) voice], "PitchGate").createXml())
        root.addChildElement(pGate.release());

    root.writeTo(dir.getChildFile(safe + "." + getPerSlotPresetExtension()));
}

void PluginProcessor::loadVoicePreset(int voice, const juce::File& file)
{
    if (voice < 0 || voice >= kMaxVoices || ! file.existsAsFile()) return;
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || ! xml->hasTagName("MuTantVoice"))
    {
        if (onLoadError) onLoadError("Could not read \"" + file.getFileName() + "\"");
        return;
    }
    auto tree = juce::ValueTree::fromXml(*xml);

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

// Apply a per-voice (.muPattern) preset from a parsed tree (shared by the
// stopped load + the boundary commit). Per-pattern spinlocks guard the audio
// thread, so no voicesLock is needed here.
// ── X-Mod preset migration (best-effort) ─────────────────────────────────────
// Old presets/patches carry v{N}_xmod_fm / _am / _ring (+ modulator assignments to
// xmod.fm/.am/.ring). The 2-lane redesign replaces them. Map on load: old FM depth →
// Lane A index in PM mode (the old "FM" was phase-mod); old AM/Ring → Lane B depth in
// Mult mode (lossy if both were set); remap modulator dest ids. No-op on new presets.
namespace
{
    juce::ValueTree findParamNode(const juce::ValueTree& state, const juce::String& id)
    {
        for (int i = 0; i < state.getNumChildren(); ++i)
        {
            auto c = state.getChild(i);
            if (c.hasType(juce::Identifier("PARAM")) && c.getProperty("id").toString() == id)
                return c;
        }
        return {};
    }

    void setParamNode(juce::ValueTree& state, const juce::String& id, float value)
    {
        auto p = findParamNode(state, id);
        if (p.isValid()) { p.setProperty("value", value, nullptr); return; }
        juce::ValueTree np(juce::Identifier("PARAM"));
        np.setProperty("id", id, nullptr);
        np.setProperty("value", value, nullptr);
        state.appendChild(np, nullptr);
    }

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

    // Full-state migration: per-voice PARAM nodes + the <VoiceData> modulator assignments.
    void migrateXModFullState(juce::ValueTree& state, int maxVoices)
    {
        for (int v = 0; v < maxVoices; ++v)
        {
            const juce::String pre = "v" + juce::String(v) + "_";
            auto fm = findParamNode(state, pre + "xmod_fm");
            if (! fm.isValid()) continue;                                  // not an old voice
            if (findParamNode(state, pre + "xmod_index").isValid()) continue;  // already migrated
            const float fmv = (float) fm.getProperty("value");
            auto  am  = findParamNode(state, pre + "xmod_am");
            auto  rg  = findParamNode(state, pre + "xmod_ring");
            const float amv = am.isValid() ? (float) am.getProperty("value") : 0.0f;
            const float rgv = rg.isValid() ? (float) rg.getProperty("value") : 0.0f;
            setParamNode(state, pre + "xmod_index",     fmv);              // FM depth → index
            setParamNode(state, pre + "xmod_phaseMode", 1.0f);            // old FM was phase-mod → PM
            // Old AM vs Ring → the matching amp mode + depth (the larger wins if both set).
            const bool amWins = amv >= rgv;
            setParamNode(state, pre + "xmod_depth",   amWins ? amv : rgv);
            setParamNode(state, pre + "xmod_ampMode", amWins ? 0.0f : 1.0f);   // AM : RM
        }
        migrateModDestIds(state.getChildWithName("VoiceData"));
    }
}

void PluginProcessor::applyVoicePresetTree(int voice, const juce::ValueTree& tree)
{
    if (voice < 0 || voice >= kMaxVoices) return;
    const juce::String prefix = juce::String("v") + juce::String(voice) + "_";

    // Params — voice-agnostic base id → v{voice}_ param, normalised 0..1. Old X-Mod
    // ids (xmod_fm/am/ring) are intercepted for best-effort migration to the 2-lane model.
    bool  sawOldXmodFm = false, sawNewXmodIndex = false;
    float oldFmNorm = 0.0f, oldAmNorm = 0.0f, oldRingNorm = 0.0f;   // stored values are normalised 0..1
    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        const auto child = tree.getChild(i);
        if (! child.hasType(juce::Identifier("p"))) continue;
        const juce::String base = child.getProperty("id").toString();
        const float vn = (float) (double) child.getProperty("v");
        if      (base == "xmod_fm")    { oldFmNorm   = vn; sawOldXmodFm = true; continue; }   // param gone
        else if (base == "xmod_am")    { oldAmNorm   = vn; continue; }
        else if (base == "xmod_ring")  { oldRingNorm = vn; continue; }
        if (base == "xmod_index") sawNewXmodIndex = true;
        if (auto* p = apvts.getParameter(prefix + base))
            p->setValueNotifyingHost(vn);
    }
    // Best-effort X-Mod migration for an old .muPattern (old ranges were all 0..100,
    // so the normalised FM value maps straight to the same-range index; AM/Ring → depth).
    if (sawOldXmodFm && ! sawNewXmodIndex)
    {
        auto setN = [this, &prefix](const char* base, float denorm) {
            if (auto* p = apvts.getParameter(prefix + base))
                p->setValueNotifyingHost(p->convertTo0to1(denorm));
        };
        setN("xmod_index",     oldFmNorm * 100.0f);                          // 0..100
        setN("xmod_phaseMode", 1.0f);                                        // PM
        const bool amWins = oldAmNorm >= oldRingNorm;                        // larger → its mode
        setN("xmod_depth",     (amWins ? oldAmNorm : oldRingNorm) * 100.0f); // 0..100 → +depth
        setN("xmod_ampMode",   amWins ? 0.0f : 1.0f);                        // AM : RM
    }

    // Modulators + gate / filter / pitch envelopes (live outside APVTS). Absent
    // children → cleared (getChildWithName returns an invalid tree). Migrate old
    // xmod.* assignment dest ids first so they survive the redesign.
    mu_pp::clearModulators(voiceSlots[(size_t) voice]);
    {
        auto mods = tree.getChildWithName("Modulators").createCopy();
        migrateModDestIds(mods);
        mu_pp::deserialiseModulators(mods, voiceSlots[(size_t) voice], {}, isValidModDest);
    }
    refreshPitchQuantFlags(voice);   // assignments changed → refresh stepped-pitch flags
    deserialiseGate(tree.getChildWithName("Gate"),       gatePatterns[(size_t) voice]);
    deserialiseGate(tree.getChildWithName("FilterGate"), filterPatterns[(size_t) voice]);
    deserialiseGate(tree.getChildWithName("PitchGate"),  pitchPatterns[(size_t) voice]);

    // User wavetable paths → bank indices (missing file → clear to factory).
    auto loadWt = [this, voice](const juce::String& path,
                                std::array<juce::String, kMaxVoices>& paths,
                                std::array<std::atomic<int>, kMaxVoices>& idxs)
    {
        paths[(size_t) voice] = path;
        int idx = -1;
        if (path.isNotEmpty())
        {
            // Lock-free resolve first: the hot-swap stage already pre-loaded the
            // table, so the boundary commit never takes voicesLock — taking it here
            // would bail the audio render to silence for the coincident block (the
            // intermittent swap pause). Fall back to the locked disk load only when
            // not preloaded (stopped / immediate / host restore — audio silent then).
            idx = bank.findByPath(path);
            if (idx < 0)
            {
                juce::File f(path);
                if (f.existsAsFile()) { const juce::ScopedLock sl(voicesLock); idx = bank.addOrLoadFile(f); }
            }
        }
        idxs[(size_t) voice].store(idx);
    };
    loadWt(tree.getProperty("o1WtPath").toString(), osc1UserPath, osc1UserIndex);
    loadWt(tree.getProperty("o2WtPath").toString(), osc2UserPath, osc2UserIndex);
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

// Same, for a full preset: each <VoiceData>/<Voice> carries o1WtPath / o2WtPath.
void PluginProcessor::preloadWavetablesFromState(const juce::ValueTree& state)
{
    auto vd = state.getChildWithName("VoiceData");
    for (int i = 0; i < vd.getNumChildren(); ++i)
    {
        const auto voice = vd.getChild(i);
        if (voice.hasType(juce::Identifier("Voice")))
            preloadWavetablesFromVoiceTree(voice);
    }
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

void PluginProcessor::writeVoiceDataToState()
{
    // Rebuild a fresh <VoiceData> child (drop any stale one) holding each active
    // voice's modulators + gate, so apvts.copyState() carries them into the file.
    apvts.state.removeChild(apvts.state.getChildWithName("VoiceData"), nullptr);

    juce::ValueTree vd("VoiceData");
    const int n = numVoices.load();
    for (int v = 0; v < n; ++v)
    {
        juce::ValueTree voice("Voice");
        voice.setProperty("idx", v, nullptr);
        if (osc1UserPath[(size_t) v].isNotEmpty()) voice.setProperty("o1WtPath", osc1UserPath[(size_t) v], nullptr);
        if (osc2UserPath[(size_t) v].isNotEmpty()) voice.setProperty("o2WtPath", osc2UserPath[(size_t) v], nullptr);
        voice.addChild(mu_pp::serialiseModulators(voiceSlots[(size_t) v]),          -1, nullptr);
        voice.addChild(serialiseGate(gatePatterns[(size_t) v]),                     -1, nullptr);
        voice.addChild(serialiseGate(filterPatterns[(size_t) v], "FilterGate"),     -1, nullptr);
        voice.addChild(serialiseGate(pitchPatterns[(size_t) v],  "PitchGate"),      -1, nullptr);
        vd.addChild(voice, -1, nullptr);
    }
    apvts.state.addChild(vd, -1, nullptr);
}

void PluginProcessor::readVoiceDataFromState()
{
    // Clear the active voices first so loading a preset without <VoiceData> (older
    // or foreign) yields a clean slate rather than stale modulators / gates.
    auto vd = apvts.state.getChildWithName("VoiceData");

    // Clear then restore each active voice. clearModulators is required because
    // deserialiseModulators accumulates assignments; deserialiseGate self-clears.
    // An absent <VoiceData> (older / foreign preset) leaves every voice cleared.
    const int n = numVoices.load();
    for (int v = 0; v < n; ++v)
    {
        mu_pp::clearModulators(voiceSlots[(size_t) v]);
        juce::ValueTree voice;
        for (int i = 0; i < vd.getNumChildren(); ++i)
            if (vd.getChild(i).getType() == juce::Identifier("Voice")
                && (int) vd.getChild(i).getProperty("idx", -1) == v)
            { voice = vd.getChild(i); break; }

        mu_pp::deserialiseModulators(voice.getChildWithName("Modulators"),
                                     voiceSlots[(size_t) v], {}, isValidModDest);
        deserialiseGate(voice.getChildWithName("Gate"),       gatePatterns[(size_t) v]);
        deserialiseGate(voice.getChildWithName("FilterGate"), filterPatterns[(size_t) v]);
        deserialiseGate(voice.getChildWithName("PitchGate"),  pitchPatterns[(size_t) v]);

        // User wavetable paths → resolve to bank indices (load the file if present).
        // A missing file keeps the path (UI shows "missing") but resolves to -1 so
        // the oscillator falls back to its factory selection. Bank append is locked
        // (recursive CriticalSection — safe whether or not a caller already holds it).
        auto resolveWt = [this, v](const juce::String& path,
                                   std::array<juce::String, kMaxVoices>& paths,
                                   std::array<std::atomic<int>, kMaxVoices>& idxs)
        {
            paths[(size_t) v] = path;
            int idx = -1;
            if (path.isNotEmpty())
            {
                // Lock-free resolve first (preloaded at hot-swap stage time); only
                // fall back to the locked disk load when not already in the bank
                // (stopped / immediate / host restore). Keeps the boundary commit
                // off voicesLock so the audio render never bails → no swap pause.
                idx = bank.findByPath(path);
                if (idx < 0)
                {
                    juce::File f(path);
                    if (f.existsAsFile()) { const juce::ScopedLock sl(voicesLock); idx = bank.addOrLoadFile(f); }
                }
            }
            idxs[(size_t) v].store(idx);
        };
        resolveWt(voice.getProperty("o1WtPath", "").toString(), osc1UserPath, osc1UserIndex);
        resolveWt(voice.getProperty("o2WtPath", "").toString(), osc2UserPath, osc2UserIndex);
    }
    refreshAllPitchQuantFlags();   // modulators reloaded → refresh stepped-pitch flags
}

juce::File PluginProcessor::getContentDir() const
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
               .getChildFile("TDP").getChildFile("muTant");
}

juce::File PluginProcessor::getPresetsDir()     const { return getContentDir().getChildFile("Presets"); }
juce::File PluginProcessor::getPerSlotPresetDir() const { return getContentDir().getChildFile("Voices"); }
juce::File PluginProcessor::getWavetablesDir()  const { return getContentDir().getChildFile("Wavetables"); }

void PluginProcessor::savePreset(const juce::String& name, const juce::String& desc,
                                 const juce::String& category, bool /*embedSamples*/)
{
    auto dir = getPresetsDir();
    dir.createDirectory();

    juce::String safe = name.replaceCharacters("\\/:|*?<>\"", "_________");
    if (safe.isEmpty()) safe = "Preset";

    // Wrap the whole APVTS state with name / description / category metadata.
    juce::XmlElement root("MuTantPreset");
    root.setAttribute("name", name);
    root.setAttribute("description", desc);
    root.setAttribute("category", category);
    apvts.state.setProperty("numVoices", numVoices.load(), nullptr);
    apvts.state.setProperty("voiceColours", serialiseVoiceColours(), nullptr);
    writeVoiceDataToState();
    if (auto state = apvts.copyState().createXml())
        root.addChildElement(state.release());

    root.writeTo(dir.getChildFile(safe + "." + getFullPresetExtension()));
}

void PluginProcessor::loadPreset(const juce::File& file)
{
    if (! file.existsAsFile()) return;

    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr)
    {
        if (onLoadError) onLoadError("Could not read \"" + file.getFileName() + "\"");
        return;
    }

    // Accept a wrapped MuTantPreset or a bare APVTS state element.
    juce::XmlElement* stateXml = xml->hasTagName("MuTantPreset")
                               ? xml->getChildByName(apvts.state.getType().toString())
                               : xml.get();
    if (stateXml == nullptr)
    {
        if (onLoadError) onLoadError("Preset has no saved state");
        return;
    }
    auto state = juce::ValueTree::fromXml(*stateXml);

    // Hot-swap: while the transport is playing, stage the parsed state and commit
    // it at voice 0's next loop boundary (handleAsyncUpdate) so the switch is
    // musically seamless; while stopped, apply immediately. Wavetables referenced
    // by the staged state are pre-loaded into the bank now so the boundary commit
    // does no disk I/O.
    if (isInternalPlaying())
    {
        preloadWavetablesFromState(state);
        hotSwapStager.stageFull(std::move(state));
    }
    else
    {
        applyFullPresetTree(state);
    }
}

// Apply a full preset's APVTS state immediately (the shared commit path for the
// stopped load, the boundary commit, and host state restore). Swaps the tree
// under voicesLock — the audio thread tryLocks it in processBlock and outputs a
// silent block on contention, so the swap is exclusive.
void PluginProcessor::applyFullPresetTree(const juce::ValueTree& stateIn)
{
    // Migrate a mutable copy so old xmod params/assignments map to the new 2-lane model
    // before they reach the APVTS / modulator deserialise.
    juce::ValueTree state = stateIn.createCopy();
    migrateXModFullState(state, kMaxVoices);

    // No blanket voicesLock here: every structure this touches is already guarded by
    // its own fine-grained lock that the audio render respects — APVTS params are
    // atomic (replaceState), gate patterns use editLock (deserialiseGate), modulator
    // slots use modLock (deserialise/clearModulators), and the wavetable bank append
    // self-locks. Holding a blanket lock instead made the audio render bail to silence
    // AND froze the transport for the whole commit (the hot-swap glitch). Without it,
    // a concurrent block sees old-or-new per structure (≤1 block, inaudible) instead.

    // Demo cap: an unlicensed build activates at most demoMaxChannels() voices. The other
    // voices' params/data still load but stay inactive (getNumChannels() == numVoices).
    const int oldN = numVoices.load(std::memory_order_relaxed);
    int nv = juce::jlimit(1, kMaxVoices, (int) state.getProperty("numVoices", 1));
    if (! isLicensed())
        nv = juce::jmin(nv, demoMaxChannels());

    // Retire-tail: when this preset drops voices while playing, snapshot each dropped
    // voice's OLD engine + insert config NOW (before replaceState clobbers the params)
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

    apvts.replaceState(state);
    numVoices.store(nv);
    restoreVoiceColours(apvts.state.getProperty("voiceColours", "").toString());
    readVoiceDataFromState();
    syncAllFxParams();   // re-seed mixer/FX engine state (unchanged values skip listeners)
}

juce::StringArray PluginProcessor::loadCategoryList() const
{
    juce::StringArray cats;
    auto dir = getPresetsDir();
    if (dir.isDirectory())
        for (const auto& f : dir.findChildFiles(juce::File::findFiles, false,
                                                "*." + getFullPresetExtension()))
            if (auto xml = juce::XmlDocument::parse(f))
                if (xml->hasTagName("MuTantPreset"))
                {
                    const auto c = xml->getStringAttribute("category");
                    if (c.isNotEmpty()) cats.addIfNotAlreadyThere(c);
                }
    return cats;
}

} // namespace mu_tant
