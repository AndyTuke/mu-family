// #365: partial-class TU split from PluginProcessor.cpp. Contains:
// - stageRhythmPreset / cancelStagedSwap / hasPendingSwap (preset hot-swap)
// - saveRhythmPreset / loadCategoryList / ensureCategoryInList / saveRhythmPresetToFile
// - applyRhythmPreset / applyDefaultRhythm / loadDefaultPreset
// - serialiseModulators / deserialiseModulators / clearModulators (file-local)
// - populateStateTree / migrateLegacyHostState (file-local)
// - getStateInformation / restoreStateFromTree / setStateInformation
// - savePreset / loadPreset

#include "PluginProcessor.h"
#include "PluginProcessor_Internal.h"
#include <thread>   // #237: std::this_thread::yield in modulator deserialise lock-spin

using mu_pp::kRhythmSuffixes;
using mu_pp::kChannelSuffixes;
using mu_pp::kGlobalParams;
using mu_pp::applyRhythmSuffix;

// Forward decls so the save/load paths defined earlier in this TU can reference
// the modulator serialise helpers defined later.
static juce::ValueTree serialiseModulators(const Rhythm& r);
static void            deserialiseModulators(const juce::ValueTree& mods, Rhythm& r);
static void            clearModulators(Rhythm& r);

// #288: host-state format version. Bump whenever the on-disk schema changes
// in a way that requires migration on load.
//   v0 / v1 : legacy (pre-#217) — ADSR A/D/R stored as 0..100 (×0.03 → seconds)
//   v2      : current (#217/#286/#287) — ADSR A/D/R stored as 0..10 (seconds direct)
static constexpr int kCurrentStateFormatVersion = 2;

//==============================================================================
void PluginProcessor::stageRhythmPreset(int rhythmIndex, const juce::File& file)
{
    if (rhythmIndex < 0 || rhythmIndex >= sequencer.getNumRhythms()) return;

    if (!sequencerPlaying.load())
    {
        applyRhythmPreset(file, rhythmIndex);
        return;
    }

    if (!file.existsAsFile())
    {
        if (onLoadError) onLoadError("File missing: " + file.getFileName());
        return;
    }
    auto xml = juce::parseXML(file);
    if (!xml)
    {
        if (onLoadError) onLoadError("Could not parse: " + file.getFileName());
        return;
    }
    auto state = juce::ValueTree::fromXml(*xml);
    if (!state.isValid())
    {
        if (onLoadError) onLoadError("Invalid preset: " + file.getFileName());
        return;
    }

    auto& sw = pendingSwaps[rhythmIndex];

    // Cancel any existing staged swap before overwriting.
    sw.isReady.store(false, std::memory_order_release);
    sw.boundaryReached.store(false, std::memory_order_relaxed);
    sw.pendingVoice.reset();

    // Start from the current rhythm and apply the preset on top (matching applyRhythmPreset).
    Rhythm newRhythm = sequencer.getRhythm(rhythmIndex);
    const juce::String paramPrefix = "r" + juce::String(rhythmIndex) + "_";

    for (int i = 0; kRhythmSuffixes[i] != nullptr; ++i)
    {
        const juce::String suffix = kRhythmSuffixes[i];
        juce::Identifier propId { "r0_" + suffix };
        if (state.hasProperty(propId))
        {
            const float normVal = (float)state.getProperty(propId);
            if (auto* param = apvts.getParameter(paramPrefix + suffix))
            {
                const float actualVal = param->convertFrom0to1(normVal);
                bool pd = false, vd = false;
                applyRhythmSuffix(suffix, actualVal, newRhythm, pd, vd);
            }
        }
    }

    // Name and colour.
    auto nameVal = state.getProperty("r0_name");
    if (nameVal.isString() && nameVal.toString().isNotEmpty())
        newRhythm.name = nameVal.toString().toStdString();
    newRhythm.colourIndex = (int)state.getProperty("r0_colour", newRhythm.colourIndex);

    // #389: deserialise modulators from the preset. Mirrors the applyRhythmPreset
    // (stopped-state) path so hot-swap preset loads carry the preset's LFOs / step
    // sequences / matrix assignments instead of inheriting the previous rhythm's.
    // newRhythm is a local copy — no other thread holds its modLock, so the spin
    // inside clearModulators / deserialiseModulators resolves on the first attempt.
    // Legacy .muRhyth files with no Modulators child are left alone (same opt-in
    // policy as applyRhythmPreset — see [Source/PluginProcessor_Preset.cpp:281-285]).
    if (auto mods = state.getChildWithName("Modulators"); mods.isValid())
    {
        clearModulators(newRhythm);
        deserialiseModulators(mods, newRhythm);
    }

    // Prepare the pending voice engine and prime it with the preset's VoiceParams.
    // Without this, the swap commit (handleAsyncUpdate) hands the audio thread a
    // fresh VoiceEngine whose default VoiceParams (filterType=0/LP12, driveChar=0,
    // default ADSRs) get applied on the first process(); pushRhythmToAPVTS that
    // follows the swap runs under apvtsLoading=true which intentionally skips the
    // engine sync in syncRhythmParam. Result: hot-swapped rhythm plays with the
    // previous rhythm's filter/drive sound until the user touches any knob.
    auto newVoice = std::make_unique<VoiceEngine>();
    newVoice->prepareToPlay(currentSampleRate, currentBlockSize);
    newVoice->setParams(newRhythm.voiceParams);

    juce::String samplePath;
    juce::String storedPath = state.getProperty("r0_sample").toString();
    if (storedPath.isNotEmpty())
    {
        juce::File sf(storedPath);
        if (sf.existsAsFile())
        {
            newVoice->loadFile(sf);
            samplePath = sf.getFullPathName();
        }
        else
        {
            juce::File fallback = getSamplesDir().getChildFile(juce::File(storedPath).getFileName());
            if (fallback.existsAsFile())
            {
                newVoice->loadFile(fallback);
                samplePath = fallback.getFullPathName();
            }
        }
    }

    // Commit the staged data; set isReady last (release barrier).
    sw.pendingRhythm     = std::move(newRhythm);
    sw.pendingSamplePath = samplePath;
    sw.pendingVoice      = std::move(newVoice);
    sw.boundaryReached.store(false, std::memory_order_relaxed);
    sw.isReady.store(true, std::memory_order_release);
}

void PluginProcessor::cancelStagedSwap(int rhythmIndex)
{
    if (rhythmIndex < 0 || rhythmIndex >= SequencerEngine::MaxRhythms) return;
    auto& sw = pendingSwaps[rhythmIndex];
    sw.isReady.store(false, std::memory_order_release);
    sw.boundaryReached.store(false, std::memory_order_relaxed);
    sw.pendingVoice.reset();
}

bool PluginProcessor::hasPendingSwap(int rhythmIndex) const
{
    if (rhythmIndex < 0 || rhythmIndex >= SequencerEngine::MaxRhythms) return false;
    return pendingSwaps[rhythmIndex].isReady.load(std::memory_order_relaxed);
}

//==============================================================================
void PluginProcessor::saveRhythmPreset(int rhythmIdx, const juce::String& name,
                                        const juce::String& category)
{
    if (rhythmIdx < 0 || rhythmIdx >= sequencer.getNumRhythms()) return;

    juce::ValueTree state("MuClidRhythm");
    state.setProperty("presetName",     name,     nullptr);
    state.setProperty("presetCategory", category, nullptr);

    const Rhythm& r = sequencer.getRhythm(rhythmIdx);
    state.setProperty("r0_name",   juce::String(r.name),          nullptr);
    state.setProperty("r0_colour", r.colourIndex,                  nullptr);
    state.setProperty("r0_sample", loadedSamplePaths[rhythmIdx],   nullptr);

    const juce::String srcPrefix = "r" + juce::String(rhythmIdx) + "_";
    for (int i = 0; kRhythmSuffixes[i] != nullptr; ++i)
        if (auto* param = apvts.getParameter(srcPrefix + kRhythmSuffixes[i]))
            state.setProperty("r0_" + juce::String(kRhythmSuffixes[i]), param->getValue(), nullptr);

    const juce::String chPrefix = "ch" + juce::String(rhythmIdx) + "_";
    for (int i = 0; kChannelSuffixes[i] != nullptr; ++i)
        if (auto* param = apvts.getParameter(chPrefix + kChannelSuffixes[i]))
            state.setProperty("ch_" + juce::String(kChannelSuffixes[i]), param->getValue(), nullptr);

    auto dir = getRhythmsDir();
    dir.createDirectory();
    juce::String safe = name.replaceCharacters("\\/:|*?<>\"", "_________");
    if (safe.isEmpty()) safe = "Rhythm";
    dir.getChildFile(safe + ".muRhyth").replaceWithText(state.toXmlString());
}

juce::StringArray PluginProcessor::loadCategoryList() const
{
    juce::StringArray cats;
    getPresetsDir().getChildFile("categories.txt").readLines(cats);
    // Also scan .muclid and .muRhyth files for categories not yet in the list.
    auto scan = [&](const juce::File& dir, const juce::String& ext) {
        for (const auto& f : dir.findChildFiles(juce::File::findFiles, false, "*." + ext))
        {
            if (auto xml = juce::parseXML(f))
            {
                auto s = juce::ValueTree::fromXml(*xml);
                juce::String cat = s.getProperty("presetCategory", "").toString();
                if (cat.isNotEmpty() && cat != "All" && !cats.contains(cat, false))
                    cats.add(cat);
            }
        }
    };
    scan(getPresetsDir(), "muclid");
    scan(getRhythmsDir(), "muRhyth");
    cats.removeDuplicates(false);
    cats.removeEmptyStrings();
    cats.sort(false);
    return cats;
}

void PluginProcessor::ensureCategoryInList(const juce::String& cat)
{
    if (cat.isEmpty() || cat == "All" || cat == "Uncategorised") return;
    auto cats = loadCategoryList();
    if (!cats.contains(cat, false))
    {
        cats.add(cat);
        cats.sort(false);
        getPresetsDir().getChildFile("categories.txt")
                       .replaceWithText(cats.joinIntoString("\n"));
    }
}

void PluginProcessor::saveRhythmPresetToFile(int rhythmIdx, const juce::File& destFile,
                                             bool embedSample, const juce::String& category,
                                             const juce::String& description)
{
    if (rhythmIdx < 0 || rhythmIdx >= sequencer.getNumRhythms()) return;

    juce::ValueTree state("MuClidRhythm");
    state.setProperty("presetName",         destFile.getFileNameWithoutExtension(), nullptr);
    state.setProperty("presetCategory",     category,                               nullptr);
    state.setProperty("presetDescription",  description,                            nullptr);
    state.setProperty("presetEmbedSamples", embedSample ? 1 : 0,                   nullptr);

    const Rhythm& r = sequencer.getRhythm(rhythmIdx);
    state.setProperty("r0_name",   juce::String(r.name),        nullptr);
    state.setProperty("r0_colour", r.colourIndex,                nullptr);
    state.setProperty("r0_sample", loadedSamplePaths[rhythmIdx], nullptr);

    // Rhythm presets store ONLY sequencer-page state (Euclidean params, voice chain,
    // envelopes, insert effect). Mixer-page state (channel level/pan/sends/sidechain/
    // output bus) intentionally stays with the slot, not with the rhythm.
    const juce::String srcPrefix = "r" + juce::String(rhythmIdx) + "_";
    for (int i = 0; kRhythmSuffixes[i] != nullptr; ++i)
        if (auto* param = apvts.getParameter(srcPrefix + kRhythmSuffixes[i]))
            state.setProperty("r0_" + juce::String(kRhythmSuffixes[i]), param->getValue(), nullptr);

    // #237: serialise modulators (ControlSequences + ModulationMatrix assignments).
    state.addChild(serialiseModulators(sequencer.getRhythm(rhythmIdx)), -1, nullptr);

    if (embedSample)
    {
        const juce::String path = loadedSamplePaths[rhythmIdx];
        if (path.isNotEmpty())
        {
            juce::File f(path);
            if (f.existsAsFile())
            {
                juce::MemoryBlock mb;
                if (f.loadFileAsData(mb) && mb.getSize() > 0)
                {
                    state.setProperty("sampleData",
                                      juce::Base64::toBase64(mb.getData(), mb.getSize()), nullptr);
                    state.setProperty("sampleName", f.getFileName(), nullptr);
                }
            }
        }
    }

    destFile.replaceWithText(state.toXmlString());
}

bool PluginProcessor::applyRhythmPreset(const juce::File& file, int targetIdx)
{
    if (!file.existsAsFile())
    {
        if (onLoadError) onLoadError("File missing: " + file.getFileName());
        return false;
    }
    auto xml = juce::parseXML(file);
    if (!xml)
    {
        if (onLoadError) onLoadError("Could not parse: " + file.getFileName());
        return false;
    }
    auto state = juce::ValueTree::fromXml(*xml);
    if (!state.isValid())
    {
        if (onLoadError) onLoadError("Invalid preset: " + file.getFileName());
        return false;
    }

    // Load only sequencer-page state — mixer settings stay attached to the slot.
    // Older rhythm preset files may include "ch_*" properties; those are simply
    // ignored on load (no read here) so legacy files load cleanly with the new policy.
    const juce::String dstPrefix = "r" + juce::String(targetIdx) + "_";
    for (int i = 0; kRhythmSuffixes[i] != nullptr; ++i)
    {
        juce::Identifier propId { "r0_" + juce::String(kRhythmSuffixes[i]) };
        if (state.hasProperty(propId))
            if (auto* param = apvts.getParameter(dstPrefix + kRhythmSuffixes[i]))
                param->setValueNotifyingHost((float)state.getProperty(propId));
    }

    Rhythm& r = sequencer.getRhythm(targetIdx);
    auto nameVal = state.getProperty("r0_name");
    if (nameVal.isString() && nameVal.toString().isNotEmpty())
        r.name = nameVal.toString().toStdString();
    r.colourIndex = (int)state.getProperty("r0_colour", r.colourIndex);

    // #237: restore modulators if the preset carries a Modulators child.
    // Legacy .muRhyth (no Modulators) is left alone — the rhythm keeps its
    // existing in-memory CS state instead of being wiped. This is the
    // intentional opposite of clear-then-load for the host-state path: when
    // a user loads a rhythm preset that pre-dates #237, we don't want their
    // freshly-authored modulators getting destroyed.
    if (auto mods = state.getChildWithName("Modulators"); mods.isValid())
    {
        clearModulators(r);
        deserialiseModulators(mods, r);
    }

    // Embedded sample takes priority over path-based load (mirrors full-preset logic).
    juce::String encodedData = state.getProperty("sampleData").toString();
    if (encodedData.isNotEmpty())
    {
        juce::MemoryOutputStream mos;
        if (juce::Base64::convertFromBase64(mos, encodedData) && mos.getDataSize() > 0)
        {
            juce::String sampleName = state.getProperty("sampleName", "embedded").toString();
            juce::File tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile("muclid_" + sampleName);
            if (tmp.replaceWithData(mos.getData(), mos.getDataSize()))
            {
                voiceEngines[targetIdx]->loadFile(tmp);
                loadedSamplePaths.set(targetIdx, tmp.getFullPathName());
            }
        }
    }
    else
    {
        juce::String samplePath = state.getProperty("r0_sample").toString();
        if (samplePath.isNotEmpty())
        {
            juce::File f(samplePath);
            if (f.existsAsFile())
            {
                voiceEngines[targetIdx]->loadFile(f);
                loadedSamplePaths.set(targetIdx, f.getFullPathName());
            }
            else
            {
                juce::File fallback = getSamplesDir().getChildFile(juce::File(samplePath).getFileName());
                if (fallback.existsAsFile())
                {
                    voiceEngines[targetIdx]->loadFile(fallback);
                    loadedSamplePaths.set(targetIdx, fallback.getFullPathName());
                }
                else
                {
                    // Linked sample missing — drop any previously loaded sample and
                    // keep the recorded path so the RhythmPanel can show a "missing"
                    // indicator and offer a relocate-to-find browse action.
                    voiceEngines[targetIdx]->clearSample();
                    loadedSamplePaths.set(targetIdx, samplePath);
                }
            }
        }
        else
        {
            voiceEngines[targetIdx]->clearSample();
            loadedSamplePaths.set(targetIdx, juce::String());
        }
    }

    // #240: force-sync APVTS → Rhythm so freshly-defaulted slots (after a
    // shrink/grow cycle) get their fields repopulated even when JUCE skips
    // the parameterChanged listener because incoming values match the
    // already-stored APVTS values.
    forceSyncRhythmFromAPVTS(targetIdx);
    return true;
}

bool PluginProcessor::applyDefaultRhythm(int rhythmIndex)
{
    // #405: route via stageRhythmPreset so it respects play state — when playing,
    // the swap is cued at the loop boundary like every other preset-load. The
    // previous direct call to applyRhythmPreset glitched audio when used mid-play
    // (sidebar "Add Rhythm" / per-rhythm reset paths). stageRhythmPreset takes
    // the immediate-apply branch when stopped, so the stopped behaviour is
    // unchanged.
    const juce::File f = getRhythmsDir().getChildFile("_default.muRhyth");
    if (!f.existsAsFile()) return false;
    stageRhythmPreset(rhythmIndex, f);
    return true;
}

void PluginProcessor::loadDefaultPreset()
{
    juce::File f = getPresetsDir().getChildFile("_default.muclid");
    if (f.existsAsFile())
        loadPreset(f);
}

//==============================================================================
// #237 — Modulator state serialisation.
// Captures everything not APVTS-backed: per-rhythm ControlSequences
// (mode/polarity/loop+step timing/stepValues/curvePoints with Bézier handles)
// and ModulationMatrix assignments (id/source/dest/depth/curve).
//
// Schema (one Modulators child per rhythm, identified by rhythmIdx property):
//   <Modulators rhythmIdx="0">
//     <Seq id="cs0" mode="0" polarity="0"
//          loopNV="2" loopMod="0" loopMult="4"
//          stepNV="2" stepMod="0" stepMult="1">
//       <Step v="42.5"/>
//       <Point x="0.0" y="0.5" bez="0" hx="0.0" hy="0.0"/>
//     </Seq>
//     <Asgn id="..." src="cs0_output" dest="filter.cutoff" depth="50" curve="0"/>
//   </Modulators>
//
// Idempotent: a tree without a Modulators child leaves the rhythm's existing
// in-memory defaults untouched (legacy preset compat).
static juce::ValueTree serialiseModulators(const Rhythm& r)
{
    juce::ValueTree mods("Modulators");

    for (const auto& cs : r.controlSequences)
    {
        juce::ValueTree seq("Seq");
        seq.setProperty("id",       juce::String(cs.id),               nullptr);
        seq.setProperty("mode",     (int)cs.mode,                       nullptr);
        seq.setProperty("polarity", (int)cs.polarity,                   nullptr);
        seq.setProperty("loopNV",   (int)cs.loopNoteValue,              nullptr);
        seq.setProperty("loopMod",  (int)cs.loopNoteMod,                nullptr);
        seq.setProperty("loopMult", cs.loopMultiplier,                  nullptr);
        seq.setProperty("stepNV",   (int)cs.stepNoteValue,              nullptr);
        seq.setProperty("stepMod",  (int)cs.stepNoteMod,                nullptr);
        seq.setProperty("stepMult", cs.stepMultiplier,                  nullptr);

        for (const auto v : cs.stepValues)
        {
            juce::ValueTree st("Step");
            st.setProperty("v", v, nullptr);
            seq.addChild(st, -1, nullptr);
        }
        for (const auto& pt : cs.curvePoints)
        {
            juce::ValueTree p("Point");
            p.setProperty("x",   pt.x,                          nullptr);
            p.setProperty("y",   pt.y,                          nullptr);
            p.setProperty("bez", pt.hasBezierHandle ? 1 : 0,    nullptr);
            p.setProperty("hx",  pt.handleX,                    nullptr);
            p.setProperty("hy",  pt.handleY,                    nullptr);
            seq.addChild(p, -1, nullptr);
        }

        mods.addChild(seq, -1, nullptr);
    }

    for (const auto& a : r.modulationMatrix.getAssignments())
    {
        juce::ValueTree asgn("Asgn");
        asgn.setProperty("id",    juce::String(a.id),            nullptr);
        asgn.setProperty("src",   juce::String(a.sourceId),      nullptr);
        asgn.setProperty("dest",  juce::String(a.destinationId), nullptr);
        asgn.setProperty("depth", a.depth,                       nullptr);
        asgn.setProperty("curve", a.curve,                       nullptr);
        mods.addChild(asgn, -1, nullptr);
    }

    return mods;
}

static void deserialiseModulators(const juce::ValueTree& mods, Rhythm& r)
{
    if (!mods.isValid() || mods.getType() != juce::Identifier("Modulators"))
        return;

    // Spin on the rhythm's modLock so the audio thread can't be inside
    // ModulationMatrix::process() / ControlSequence::evaluate() while we
    // overwrite the vectors. Mirrors ModulatorEditor's lock pattern.
    while (r.modLock.exchange(true, std::memory_order_acquire))
        std::this_thread::yield();

    for (int ci = 0; ci < mods.getNumChildren(); ++ci)
    {
        auto node = mods.getChild(ci);

        if (node.getType() == juce::Identifier("Seq"))
        {
            const juce::String id = node.getProperty("id").toString();
            // Find matching CS by id (csN). The vector is pre-sized to
            // MaxControlSequences in Rhythm's ctor so we patch in place.
            for (auto& cs : r.controlSequences)
            {
                if (juce::String(cs.id) != id) continue;
                cs.mode           = (ControlSequence::Mode)    (int)node.getProperty("mode",     (int)cs.mode);
                cs.polarity       = (ControlSequence::Polarity)(int)node.getProperty("polarity", (int)cs.polarity);
                cs.loopNoteValue  = (NoteValue)                (int)node.getProperty("loopNV",   (int)cs.loopNoteValue);
                cs.loopNoteMod    = (NoteMod)                  (int)node.getProperty("loopMod",  (int)cs.loopNoteMod);
                cs.loopMultiplier =                            (int)node.getProperty("loopMult", cs.loopMultiplier);
                cs.stepNoteValue  = (NoteValue)                (int)node.getProperty("stepNV",   (int)cs.stepNoteValue);
                cs.stepNoteMod    = (NoteMod)                  (int)node.getProperty("stepMod",  (int)cs.stepNoteMod);
                cs.stepMultiplier =                            (int)node.getProperty("stepMult", cs.stepMultiplier);

                cs.stepValues.clear();
                cs.curvePoints.clear();
                for (int j = 0; j < node.getNumChildren(); ++j)
                {
                    auto child = node.getChild(j);
                    if (child.getType() == juce::Identifier("Step"))
                    {
                        cs.stepValues.push_back((float)(double)child.getProperty("v", 0.0));
                    }
                    else if (child.getType() == juce::Identifier("Point"))
                    {
                        ControlSequence::CurvePoint pt;
                        pt.x                = (float)(double)child.getProperty("x", 0.0);
                        pt.y                = (float)(double)child.getProperty("y", 0.0);
                        pt.hasBezierHandle  = (int)child.getProperty("bez", 0) != 0;
                        pt.handleX          = (float)(double)child.getProperty("hx", 0.0);
                        pt.handleY          = (float)(double)child.getProperty("hy", 0.0);
                        cs.curvePoints.push_back(pt);
                    }
                }
                break;
            }
        }
        else if (node.getType() == juce::Identifier("Asgn"))
        {
            ModulationAssignment a;
            a.id            = node.getProperty("id").toString().toStdString();
            a.sourceId      = node.getProperty("src").toString().toStdString();
            a.destinationId = node.getProperty("dest").toString().toStdString();
            a.depth         = (float)(double)node.getProperty("depth", 0.0);
            a.curve         = (float)(double)node.getProperty("curve", 0.0);
            r.modulationMatrix.addAssignment(a);
        }
    }

    r.modLock.store(false, std::memory_order_release);
}

// Clears CS step/curve data + assignments before each deserialise so successive
// preset loads don't accumulate state. Only called from the preset-load paths.
static void clearModulators(Rhythm& r)
{
    while (r.modLock.exchange(true, std::memory_order_acquire))
        std::this_thread::yield();

    while (!r.modulationMatrix.getAssignments().empty())
        r.modulationMatrix.removeAssignment(r.modulationMatrix.getAssignments().front().id);

    for (auto& cs : r.controlSequences)
    {
        cs.stepValues.clear();
        cs.curvePoints.clear();
    }

    r.modLock.store(false, std::memory_order_release);
}

static void populateStateTree(juce::ValueTree& state, int numRhythms,
                              SequencerEngine& seq, const juce::StringArray& samplePaths)
{
    state.setProperty("formatVersion", kCurrentStateFormatVersion, nullptr);   // #288
    state.setProperty("numRhythms", numRhythms, nullptr);
    for (int i = 0; i < numRhythms; ++i)
    {
        const Rhythm& r = seq.getRhythm(i);
        state.setProperty("r" + juce::String(i) + "_name",   juce::String(r.name),   nullptr);
        state.setProperty("r" + juce::String(i) + "_colour", r.colourIndex,           nullptr);
        state.setProperty("r" + juce::String(i) + "_sample", samplePaths[i],          nullptr);

        // #237: per-rhythm modulator state as a child of the APVTS state tree.
        auto mods = serialiseModulators(r);
        mods.setProperty("rhythmIdx", i, nullptr);
        state.addChild(mods, -1, nullptr);
    }
}

void PluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    populateStateTree(state, sequencer.getNumRhythms(), sequencer, loadedSamplePaths);
    juce::MemoryOutputStream(destData, true).writeString(state.toXmlString());
}

// #288: in-place migration of pre-#217 host state. APVTS stores parameter
// values as <PARAM id="..." value="..."/> children of the root tree. For
// legacy state (formatVersion absent or < 2), the ADSR A/D/R values lived in
// 0..100; the new schema stores them in 0..10 seconds directly. Migration:
// multiply by 0.03 (the old display→seconds factor), clamp to the new max,
// and preserve the End-mode sentinel on aEnvRel (old slider max 100 → new
// slider max 10 means "play to natural sample end").
static void migrateLegacyHostState(juce::ValueTree& state)
{
    const int version = (int)state.getProperty("formatVersion", 0);
    if (version >= kCurrentStateFormatVersion) return;

    auto isAdsrTimeSuffix = [](const juce::String& suffix) -> bool {
        return suffix == "aEnvAtk" || suffix == "aEnvDec" || suffix == "aEnvRel"
            || suffix == "fEnvAtk" || suffix == "fEnvDec" || suffix == "fEnvRel"
            || suffix == "pEnvAtk" || suffix == "pEnvDec" || suffix == "pEnvRel";
    };

    const juce::Identifier paramType   ("PARAM");
    const juce::Identifier idProperty  ("id");
    const juce::Identifier valProperty ("value");

    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        auto child = state.getChild(i);
        if (child.getType() != paramType) continue;

        const juce::String id = child.getProperty(idProperty).toString();
        // Match r{0-7}_<suffix>
        if (id.length() < 4 || id[0] != 'r' || id[1] < '0' || id[1] > '7' || id[2] != '_')
            continue;

        const juce::String suffix = id.substring(3);
        if (!isAdsrTimeSuffix(suffix)) continue;

        const float oldVal = (float)child.getProperty(valProperty, 0.0);
        float newVal = juce::jlimit(0.0f, 10.0f, oldVal * 0.03f);

        // aEnvRel End-mode sentinel: old max (100) → new max (10).
        if (suffix == "aEnvRel" && oldVal >= 100.0f) newVal = 10.0f;

        child.setProperty(valProperty, newVal, nullptr);
    }

    state.setProperty("formatVersion", kCurrentStateFormatVersion, nullptr);
}

void PluginProcessor::restoreStateFromTree(const juce::ValueTree& state)
{
    int n = juce::jlimit(1, SequencerEngine::MaxRhythms,
                         (int)state.getProperty("numRhythms", 1));

    // Expand to MaxRhythms so parameterChanged can write to all 8 rhythm slots.
    sequencer.setNumRhythms(SequencerEngine::MaxRhythms);

    // #288: migrate legacy state in-place before pushing it into APVTS so the
    // new 0..10 s ADSR ranges don't clamp old 0..100 values to absurd attacks.
    juce::ValueTree migrated = state.createCopy();
    migrateLegacyHostState(migrated);

    {
        mu_core::ScopedApvtsLoading guard(apvtsLoading);
        apvts.replaceState(migrated);
    }

    // Trim to actual active count.
    sequencer.setNumRhythms(n);

    // #237: restore per-rhythm modulator state from the Modulators children.
    // Each child carries a rhythmIdx property so we apply to the right slot
    // regardless of child ordering. Legacy state (no Modulators children)
    // leaves rhythm defaults in place — clean degradation.
    for (int ci = 0; ci < state.getNumChildren(); ++ci)
    {
        auto child = state.getChild(ci);
        if (child.getType() != juce::Identifier("Modulators")) continue;
        const int ri = (int)child.getProperty("rhythmIdx", -1);
        if (ri < 0 || ri >= n) continue;
        Rhythm& target = sequencer.getRhythm(ri);
        clearModulators(target);
        deserialiseModulators(child, target);
    }

    // Populate fixed voice/midi arrays to match n.
    // Ordering matters: when shrinking, store the new (smaller) count BEFORE destroying
    // excess slots so the audio thread can't access a slot being reset.  When expanding,
    // create and prepare slots BEFORE incrementing the count so the audio thread never
    // sees an uninitialised slot.
    const int oldN = numActiveRhythms.load(std::memory_order_acquire);
    if (n < oldN)
    {
        numActiveRhythms.store(n, std::memory_order_release);  // decrement first
        for (int i = n; i < oldN; ++i)
        {
            voiceEngines[i].reset();
            midiEngines[i] = MidiOutputEngine{};
        }
    }
    else
    {
        for (int i = oldN; i < n; ++i)
        {
            voiceEngines[i] = std::make_unique<VoiceEngine>();
            if (currentSampleRate > 0 && currentBlockSize > 0)
            {
                voiceEngines[i]->prepareToPlay(currentSampleRate, currentBlockSize);
                midiEngines[i].prepare(currentSampleRate, currentBlockSize);
            }
        }
        numActiveRhythms.store(n, std::memory_order_release);  // increment after slots ready
    }

    // Restore non-APVTS properties and refresh engines.
    for (int i = 0; i < n; ++i)
    {
        Rhythm& r = sequencer.getRhythm(i);
        r.name        = state.getProperty("r" + juce::String(i) + "_name",
                                          "Rhythm " + juce::String(i + 1)).toString().toStdString();
        r.colourIndex = (int)state.getProperty("r" + juce::String(i) + "_colour", i % 30);

        // #240: force-sync APVTS → Rhythm so shrink/grow cycles (preset A → B → A)
        // repopulate freshly-defaulted Rhythm fields even when JUCE skips listener
        // callbacks because the APVTS values didn't change. Internally calls
        // updatePattern + voiceEngines[i]->setParams.
        forceSyncRhythmFromAPVTS(i);

        juce::String samplePath = state.getProperty("r" + juce::String(i) + "_sample").toString();
        juce::String sampleData = state.getProperty("r" + juce::String(i) + "_sampleData").toString();
        juce::String sampleName = state.getProperty("r" + juce::String(i) + "_sampleName").toString();

        loadedSamplePaths.set(i, samplePath);

        if (sampleData.isNotEmpty() && sampleName.isNotEmpty())
        {
            juce::MemoryBlock mb;
            {
                juce::MemoryOutputStream mos(mb, false);
                juce::Base64::convertFromBase64(mos, sampleData);
            }
            if (mb.getSize() > 0)
            {
                juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                         .getChildFile("muClid_samples");
                tempDir.createDirectory();
                juce::File tempFile = tempDir.getChildFile(sampleName);
                if (tempFile.replaceWithData(mb.getData(), mb.getSize()))
                {
                    voiceEngines[i]->loadFile(tempFile);
                    loadedSamplePaths.set(i, tempFile.getFullPathName());
                }
            }
        }
        else if (samplePath.isNotEmpty())
        {
            juce::File f(samplePath);
            if (f.existsAsFile())
            {
                voiceEngines[i]->loadFile(f);
            }
            else
            {
                juce::File fallback = getSamplesDir().getChildFile(juce::File(samplePath).getFileName());
                if (fallback.existsAsFile())
                {
                    voiceEngines[i]->loadFile(fallback);
                    loadedSamplePaths.set(i, fallback.getFullPathName());
                }
            }
        }
    }
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // #250: in standalone, the user's saved `_default.muclid` is authoritative
    // on every launch — JUCE's auto-saved "filterState" should NOT override it.
    // The host (DAW) path still needs setStateInformation to restore project
    // state, so this override only fires when running standalone.
    if (wrapperType == wrapperType_Standalone)
    {
        const juce::File defaultPreset = getPresetsDir().getChildFile("_default.muclid");
        if (defaultPreset.existsAsFile())
        {
            loadPreset(defaultPreset);
            return;
        }
        // No default preset saved — fall through to JUCE's auto-restore.
    }

    if (auto xml = juce::parseXML(juce::String::fromUTF8((const char*)data, sizeInBytes)))
    {
        auto state = juce::ValueTree::fromXml(*xml);
        if (state.isValid())
            restoreStateFromTree(state);
        else if (onLoadError)
            onLoadError("Host state restore failed: invalid tree");
    }
    else if (onLoadError)
    {
        onLoadError("Host state restore failed: could not parse XML");
    }
}

//==============================================================================
void PluginProcessor::savePreset(const juce::String& name,
                                 const juce::String& description,
                                 const juce::String& category,
                                 bool embedSamples)
{
    const int n = sequencer.getNumRhythms();

    juce::ValueTree root("MuClidPreset");
    root.setProperty("presetName",         name,                 nullptr);
    root.setProperty("presetDescription",  description,          nullptr);
    root.setProperty("presetEmbedSamples", embedSamples ? 1 : 0, nullptr);
    root.setProperty("presetCategory",    category,    nullptr);

    for (int i = 0; i < n; ++i)
    {
        const Rhythm& r = sequencer.getRhythm(i);
        juce::ValueTree rTree("Rhythm");
        rTree.setProperty("name",   juce::String(r.name), nullptr);
        rTree.setProperty("colour", r.colourIndex,         nullptr);
        rTree.setProperty("sample", loadedSamplePaths[i],  nullptr);

        const juce::String srcPrefix = "r" + juce::String(i) + "_";
        for (int j = 0; kRhythmSuffixes[j] != nullptr; ++j)
            if (auto* param = apvts.getParameter(srcPrefix + kRhythmSuffixes[j]))
                rTree.setProperty(kRhythmSuffixes[j], param->getValue(), nullptr);

        const juce::String chSrcPrefix = "ch" + juce::String(i) + "_";
        for (int j = 0; kChannelSuffixes[j] != nullptr; ++j)
            if (auto* param = apvts.getParameter(chSrcPrefix + kChannelSuffixes[j]))
                rTree.setProperty("ch_" + juce::String(kChannelSuffixes[j]), param->getValue(), nullptr);

        // #237: serialise modulators per rhythm.
        rTree.addChild(serialiseModulators(r), -1, nullptr);

        if (embedSamples)
        {
            const juce::String path = loadedSamplePaths[i];
            if (path.isNotEmpty())
            {
                juce::File f(path);
                if (f.existsAsFile())
                {
                    juce::MemoryBlock mb;
                    if (f.loadFileAsData(mb) && mb.getSize() > 0)
                    {
                        rTree.setProperty("sampleData",
                                           juce::Base64::toBase64(mb.getData(), mb.getSize()), nullptr);
                        rTree.setProperty("sampleName", f.getFileName(), nullptr);
                    }
                }
            }
        }

        root.addChild(rTree, -1, nullptr);
    }

    // Save global FX/mixer state so a preset fully restores the session.
    juce::ValueTree globalTree("GlobalState");
    for (int i = 0; kGlobalParams[i] != nullptr; ++i)
        if (auto* param = apvts.getParameter(kGlobalParams[i]))
            globalTree.setProperty(kGlobalParams[i], param->getValue(), nullptr);
    root.addChild(globalTree, -1, nullptr);

    auto dir = getPresetsDir();
    dir.createDirectory();

    juce::String safeName = name.replaceCharacters("\\/:|*?<>\"", "_________");
    if (safeName.isEmpty()) safeName = "Preset";
    dir.getChildFile(safeName + ".muclid").replaceWithText(root.toXmlString());
}

void PluginProcessor::loadPreset(const juce::File& file)
{
    auto xml = juce::parseXML(file);
    if (!xml)
    {
        if (onLoadError) onLoadError("Could not parse: " + file.getFileName());
        return;
    }
    auto root = juce::ValueTree::fromXml(*xml);
    if (!root.isValid())
    {
        if (onLoadError) onLoadError("Invalid preset: " + file.getFileName());
        return;
    }

    if (root.getType() == juce::Identifier("MuClidPreset"))
    {
        // Count only Rhythm-type children; GlobalState child was added in #123.
        int n = 0;
        for (int ci = 0; ci < root.getNumChildren(); ++ci)
            if (root.getChild(ci).getType() == juce::Identifier("Rhythm"))
                ++n;
        n = juce::jlimit(1, SequencerEngine::MaxRhythms, n);
        sequencer.setNumRhythms(n);

        const int oldN2 = numActiveRhythms.load(std::memory_order_acquire);
        if (n < oldN2)
        {
            numActiveRhythms.store(n, std::memory_order_release);
            for (int i = n; i < oldN2; ++i)
            {
                voiceEngines[i].reset();
                midiEngines[i] = MidiOutputEngine{};
                // Wipe mixer channel state and sample-path memory so the now-inactive
                // slot does not retain stale fader/sidechain/sample data from the
                // pre-load session. Without this, opening the mixer after loading a
                // smaller preset would still show the previous tenant's settings on
                // the dimmed slots.
                mixerEngine.channels[i] = MixerEngine::ChannelState{};
                if (i < loadedSamplePaths.size())
                    loadedSamplePaths.set(i, juce::String());
            }
        }
        else
        {
            for (int i = oldN2; i < n; ++i)
            {
                voiceEngines[i] = std::make_unique<VoiceEngine>();
                if (currentSampleRate > 0 && currentBlockSize > 0)
                {
                    voiceEngines[i]->prepareToPlay(currentSampleRate, currentBlockSize);
                    midiEngines[i].prepare(currentSampleRate, currentBlockSize);
                }
            }
            numActiveRhythms.store(n, std::memory_order_release);
        }

        mu_core::ScopedApvtsLoading guard(apvtsLoading);
        int rhythmIdx = 0;
        for (int ci = 0; ci < root.getNumChildren() && rhythmIdx < n; ++ci)
        {
            auto rTree = root.getChild(ci);
            if (rTree.getType() != juce::Identifier("Rhythm")) continue;
            const int i = rhythmIdx++;

            const juce::String dstPrefix = "r" + juce::String(i) + "_";
            for (int j = 0; kRhythmSuffixes[j] != nullptr; ++j)
            {
                juce::Identifier propId { kRhythmSuffixes[j] };
                if (rTree.hasProperty(propId))
                    if (auto* param = apvts.getParameter(dstPrefix + kRhythmSuffixes[j]))
                        param->setValueNotifyingHost((float)rTree.getProperty(propId));
            }

            const juce::String dstChPrefix = "ch" + juce::String(i) + "_";
            for (int j = 0; kChannelSuffixes[j] != nullptr; ++j)
            {
                juce::Identifier chPropId { "ch_" + juce::String(kChannelSuffixes[j]) };
                if (rTree.hasProperty(chPropId))
                    if (auto* param = apvts.getParameter(dstChPrefix + kChannelSuffixes[j]))
                        param->setValueNotifyingHost((float)rTree.getProperty(chPropId));
            }

            Rhythm& r = sequencer.getRhythm(i);

            // #237: restore modulators if the preset carries a Modulators child.
            // Same opt-in behaviour as applyRhythmPreset — legacy .muclid files
            // (no Modulators child per rhythm) leave the rhythm's existing
            // in-memory state in place, avoiding accidental destruction.
            if (auto rMods = rTree.getChildWithName("Modulators"); rMods.isValid())
            {
                clearModulators(r);
                deserialiseModulators(rMods, r);
            }

            auto nameVal = rTree.getProperty("name");
            if (nameVal.isString() && nameVal.toString().isNotEmpty())
                r.name = nameVal.toString().toStdString();
            r.colourIndex = (int)rTree.getProperty("colour", r.colourIndex);

            juce::String sampleData = rTree.getProperty("sampleData").toString();
            juce::String sampleName = rTree.getProperty("sampleName").toString();
            juce::String samplePath = rTree.getProperty("sample").toString();
            loadedSamplePaths.set(i, samplePath);

            if (sampleData.isNotEmpty() && sampleName.isNotEmpty())
            {
                juce::MemoryBlock mb;
                {
                    juce::MemoryOutputStream mos(mb, false);
                    juce::Base64::convertFromBase64(mos, sampleData);
                }
                if (mb.getSize() > 0)
                {
                    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                             .getChildFile("muClid_samples");
                    tempDir.createDirectory();
                    juce::File tempFile = tempDir.getChildFile(sampleName);
                    if (tempFile.replaceWithData(mb.getData(), mb.getSize()))
                    {
                        voiceEngines[i]->loadFile(tempFile);
                        loadedSamplePaths.set(i, tempFile.getFullPathName());
                    }
                }
            }
            else if (samplePath.isNotEmpty())
            {
                juce::File f(samplePath);
                if (f.existsAsFile())
                {
                    voiceEngines[i]->loadFile(f);
                }
                else
                {
                    juce::File fallback = getSamplesDir().getChildFile(juce::File(samplePath).getFileName());
                    if (fallback.existsAsFile())
                    {
                        voiceEngines[i]->loadFile(fallback);
                        loadedSamplePaths.set(i, fallback.getFullPathName());
                    }
                    else
                    {
                        // Linked sample not found — clear any previously loaded sample
                        // on this slot so the RhythmPanel shows the missing indicator
                        // consistently and the user can click to relocate.
                        voiceEngines[i]->clearSample();
                        // loadedSamplePaths already set to samplePath above (line ~1999)
                    }
                }
            }
            else
            {
                // Preset rhythm has no sample at all — wipe any stale sample on this slot.
                voiceEngines[i]->clearSample();
            }

            // #240: force-sync APVTS → Rhythm so a preset that re-loads identical
            // values into a freshly-recreated slot (preset A → B → A pattern)
            // still populates r.voiceParams / r.genA.hits. JUCE skips listener
            // callbacks when setValueNotifyingHost is called with an unchanged
            // value, so parameterChanged → syncRhythmParam never fires.
            // Internally calls updatePattern + voiceEngines[i]->setParams.
            forceSyncRhythmFromAPVTS(i);
        }

        // Restore global FX/mixer state if present (added in #123; older files omit this).
        for (int ci = 0; ci < root.getNumChildren(); ++ci)
        {
            auto child = root.getChild(ci);
            if (child.getType() != juce::Identifier("GlobalState")) continue;
            for (int gi = 0; kGlobalParams[gi] != nullptr; ++gi)
            {
                juce::Identifier propId { kGlobalParams[gi] };
                if (child.hasProperty(propId))
                    if (auto* param = apvts.getParameter(kGlobalParams[gi]))
                        param->setValueNotifyingHost((float)child.getProperty(propId));
            }
            break;
        }
        // #391: ScopedApvtsLoading guard goes out of scope at the closing brace below.
    }
    else
    {
        restoreStateFromTree(root);
    }
}
