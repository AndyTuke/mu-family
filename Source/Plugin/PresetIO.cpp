#include "PresetIO.h"
#include "PluginProcessor.h"
#include "PluginProcessor_Internal.h"
#include "Persistence/PresetHelpers.h"      // writeKindedProperty, readKindedPropertyAsActualV2, kGlobalParamDefs
#include "Persistence/ModulatorSerialise.h" // serialiseModulators, deserialiseModulators, clearModulators
#include <limits>               // std::numeric_limits for NaN sentinel

using mu_pp::kRhythmParamDefs;
using mu_pp::kRhythmParamCount;
using mu_pp::kChannelSuffixes;
using mu_pp::kGlobalParams;
using mu_pp::applyRhythmSuffix;
using mu_pp::kCurrentPresetVersion;
using mu_pp::writeKindedProperty;
using mu_pp::readKindedPropertyAsActualV2;
using mu_pp::kGlobalParamDefs;
using mu_pp::kGlobalParamDefCount;
using mu_pp::serialiseModulators;
using mu_pp::deserialiseModulators;
using mu_pp::clearModulators;
using mu_pp::enumName;
using mu_pp::readEnumIndex;

// serialiseModulators / deserialiseModulators / clearModulators are defined
// as inline functions in ModulatorSerialise.h (brought in via the using
// declarations above), so no forward declarations are needed here.

// host-state format version. Bump whenever the on-disk schema changes
// in a way that requires migration on load.
//   v0 / v1 : legacy (pre-#217) — ADSR A/D/R stored as 0..100 (×0.03 → seconds)
//   v2      : current (#217/#286/#287) — ADSR A/D/R stored as 0..10 (seconds direct)
static constexpr int kCurrentStateFormatVersion = 2;

// Stage 35: write `actualValue` into `tree` based on the param's `ParamKind`.
// v2 produces human-readable + range-stable values:
//   ParamKind::Bool            → "true" / "false"
//   ParamKind::Int             → integer property
//   ParamKind::AlgorithmIndex  → stable algorithm name string (e.g. "Bitcrusher")
//   ParamKind::Float           → actual de-normalised value
//
// writeKindedProperty is now inline in PresetHelpers.h — see using declaration above.

// Convenience wrapper for the per-rhythm save path — pulls actual value out of
// the APVTS param and delegates to writeKindedProperty.
static void writeParamPropertyV2(juce::ValueTree& tree,
                                 const juce::String& propName,
                                 const juce::RangedAudioParameter& param,
                                 const mu_pp::RhythmParamDef& def)
{
    const float actual = param.convertFrom0to1(param.getValue());
    writeKindedProperty(tree, propName, actual, def.kind, def.algorithmNames);
}

// readKindedPropertyAsActualV2 is now inline in PresetHelpers.h — see using declaration above.

// Per-rhythm read wrapper. v2-only: caller must have already verified
// presetVersion via requireSupportedPresetVersion before reaching here.
// `param` is unused but kept on the signature for symmetry with the global
// reader and so future format changes can fall back to APVTS-range-aware
// conversion without a signature churn.
static float readParamPropertyAsActual(const juce::ValueTree& tree,
                                        const juce::String& propName,
                                        const juce::RangedAudioParameter& /*param*/,
                                        const mu_pp::RhythmParamDef& def)
{
    if (! tree.hasProperty(propName))
        return std::numeric_limits<float>::quiet_NaN();
    return readKindedPropertyAsActualV2(tree, propName, def.kind, def.algorithmNames);
}

// Global-state read wrapper. v2-only (see readParamPropertyAsActual above).
static float readGlobalPropertyAsActual(const juce::ValueTree& tree,
                                         const juce::String& propName,
                                         const juce::RangedAudioParameter& /*param*/,
                                         const mu_pp::GlobalParamDef& def)
{
    if (! tree.hasProperty(propName))
        return std::numeric_limits<float>::quiet_NaN();
    return readKindedPropertyAsActualV2(tree, propName, def.kind, def.algorithmNames);
}

// Reject any preset file that doesn't declare presetVersion = kCurrentPresetVersion.
// Returns true if the version is supported; false if the caller should bail.
// On rejection, fires proc_.onLoadError with a clear message so the user knows the file
// is in an obsolete format and needs to be re-saved (or hand-converted by Andy).
static bool requireSupportedPresetVersion(const juce::ValueTree& tree,
                                          const juce::String& fileName,
                                          const std::function<void(const juce::String&)>& onLoadError)
{
    const int v = (int) tree.getProperty("presetVersion", 0);
    if (v == mu_pp::kCurrentPresetVersion)
        return true;
    if (onLoadError)
    {
        onLoadError("Preset '" + fileName + "' is in legacy format v" + juce::String(v)
                    + " (current is v" + juce::String(mu_pp::kCurrentPresetVersion)
                    + "). Legacy presets are not auto-migrated — paste the XML to the dev to upgrade.");
    }
    return false;
}

// detect a sample path that points into our embedded-sample decode temp
// dir (%TEMP%/muClid_samples/). These paths come from loading a preset whose
// `sampleData` was base64-decoded into a temp file by the load path; they are
// NOT durable references — the temp dir gets wiped between OS sessions, and
// any subsequent .muRhyth / .muclid save that records the temp path as
// `r0_sample` would silently break on next load. Used by the save flow below
// to force-embed instead of writing the temp path.
static bool isEmbeddedSampleTempPath(const juce::String& path)
{
    if (path.isEmpty()) return false;
    const juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .getChildFile("muClid_samples");
    return tempDir.exists() && juce::File(path).isAChildOf(tempDir);
}

// write `text` to `destFile` atomically. Goes via a sibling juce::TemporaryFile
// so a power-loss / crash / antivirus interruption mid-write cannot corrupt the
// destination — either the new bytes land in full or the original (if it existed)
// stays intact. Wraps the previous `destFile.replaceWithText(text)` call sites.
// Reports failures via proc_.onLoadError (best we can do — the disk is in trouble).
static bool atomicReplaceWithText(const juce::File& destFile, const juce::String& text,
                                  const std::function<void(const juce::String&)>& onLoadError)
{
    juce::TemporaryFile tmp(destFile);
    if (! tmp.getFile().replaceWithText(text))
    {
        if (onLoadError) onLoadError("Could not write temp file for: " + destFile.getFileName());
        return false;
    }
    if (! tmp.overwriteTargetFileWithTemporary())
    {
        if (onLoadError) onLoadError("Could not rename temp file over: " + destFile.getFileName());
        return false;
    }
    return true;
}

//==============================================================================
void PresetIO::stageRhythmPreset(int rhythmIndex, const juce::File& file)
{
    if (rhythmIndex < 0 || rhythmIndex >= proc_.sequencer.getNumRhythms()) return;

    if (!proc_.sequencerPlaying.load())
    {
        applyRhythmPreset(file, rhythmIndex);
        return;
    }

    if (!file.existsAsFile())
    {
        if (proc_.onLoadError) proc_.onLoadError("File missing: " + file.getFileName());
        return;
    }
    auto xml = juce::parseXML(file);
    if (!xml)
    {
        if (proc_.onLoadError) proc_.onLoadError("Could not parse: " + file.getFileName());
        return;
    }
    auto state = juce::ValueTree::fromXml(*xml);
    if (!state.isValid())
    {
        if (proc_.onLoadError) proc_.onLoadError("Invalid preset: " + file.getFileName());
        return;
    }

    // Cancel any existing staged swap before overwriting.
    proc_.hotSwapStager.cancelPendingIfAny(rhythmIndex);

    // Start from the current rhythm and apply the preset on top (matching applyRhythmPreset).
    Rhythm newRhythm = proc_.sequencer.getRhythm(rhythmIndex);
    const juce::String paramPrefix = "r" + juce::String(rhythmIndex) + "_";

    // Stage 35 / cleanup: only v2 presets are supported. Legacy (v0/v1) presets
    // are refused at the entry point with a clear proc_.onLoadError; the user is
    // expected to hand-upgrade them via the dev. Keeps the loader simple.
    if (! requireSupportedPresetVersion(state, file.getFileName(), proc_.onLoadError))
        return;

    for (int i = 0; i < kRhythmParamCount; ++i)
    {
        const auto& def = kRhythmParamDefs[i];
        const juce::String propName = "r0_" + juce::String(def.suffix);
        if (auto* param = proc_.apvts.getParameter(paramPrefix + def.suffix))
        {
            const float actualVal = readParamPropertyAsActual(state, propName, *param, def);
            if (! std::isnan(actualVal))
            {
                bool pd = false, vd = false;
                applyRhythmSuffix(def.suffix, actualVal, newRhythm, pd, vd);
            }
        }
    }

    // Name and colour.
    auto nameVal = state.getProperty("r0_name");
    if (nameVal.isString() && nameVal.toString().isNotEmpty())
        newRhythm.name = nameVal.toString().toStdString();
    newRhythm.colourIndex = (int)state.getProperty("r0_colour", newRhythm.colourIndex);

    // deserialise modulators from the preset. Mirrors the applyRhythmPreset
    // (stopped-state) path so hot-swap preset loads carry the preset's LFOs / step
    // sequences / matrix assignments instead of inheriting the previous rhythm's.
    // newRhythm is a local copy — no other thread holds its modLock, so the spin
    // inside clearModulators / deserialiseModulators resolves on the first attempt.
    // Legacy .muRhyth files with no Modulators child are left alone (same opt-in
    // policy as applyRhythmPreset — see [Source/PluginProcessor_Preset.cpp:281-285]).
    if (auto mods = state.getChildWithName("Modulators"); mods.isValid())
    {
        clearModulators(newRhythm);
        auto dropped = deserialiseModulators(mods, newRhythm);
        if (! dropped.isEmpty() && proc_.onLoadError)
            proc_.onLoadError("Dropped " + juce::String(dropped.size())
                        + " modulator assignment(s) from " + file.getFileName()
                        + ": " + dropped.joinIntoString("; "));
    }

    // Prepare the pending voice engine and prime it with the preset's VoiceParams.
    // Without this, the swap commit (handleAsyncUpdate) hands the audio thread a
    // fresh VoiceEngine whose default VoiceParams (filterType=0/LP12, driveChar=0,
    // default ADSRs) get applied on the first process(); pushRhythmToAPVTS that
    // follows the swap runs under proc_.apvtsLoading=true which intentionally skips the
    // engine sync in syncRhythmParam. Result: hot-swapped rhythm plays with the
    // previous rhythm's filter/drive sound until the user touches any knob.
    auto newVoice = std::make_unique<VoiceEngine>();
    newVoice->prepareToPlay(proc_.currentSampleRate, proc_.currentBlockSize);
    newVoice->setParams(newRhythm.voiceParams);

    juce::String samplePath;
    // Embedded sample takes priority over path-based load — mirrors
    // applyRhythmPreset's policy. Without this branch, hot-swapping a preset
    // that was saved with embedSample=true (so r0_sample is empty by design,
    // and the bytes live in <sampleData>) silently lands with no audio.
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
                newVoice->loadFile(tmp);
                samplePath = tmp.getFullPathName();
            }
        }
    }
    else
    {
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
                juce::File fallback = proc_.getSamplesDir().getChildFile(juce::File(storedPath).getFileName());
                if (fallback.existsAsFile())
                {
                    newVoice->loadFile(fallback);
                    samplePath = fallback.getFullPathName();
                    // Surface the relocation so the user notices when a
                    // same-basename file from the content samples dir got picked
                    // up instead of the original referenced path. Different files
                    // with the same basename are a real hazard — a "kick.wav" in
                    // the project samples dir might be a completely different
                    // recording than the one originally loaded into the preset.
                    if (proc_.onLoadError)
                        proc_.onLoadError("Sample '" + juce::File(storedPath).getFileName()
                                    + "' not at original path, loaded from content folder instead.");
                }
                else
                {
                    // Linked sample missing — keep the recorded path so the
                    // RhythmPanel "missing — click to find" affordance fires,
                    // and warn so the silent-output case has a visible cause.
                    samplePath = storedPath;
                    if (proc_.onLoadError)
                        proc_.onLoadError("Sample '" + juce::File(storedPath).getFileName()
                                    + "' missing — rhythm loaded without audio.");
                }
            }
        }
    }

    // Commit the staged data via HotSwapStager.
    proc_.hotSwapStager.stage(rhythmIndex, std::move(newRhythm), std::move(newVoice), samplePath);
}

//==============================================================================
// PluginProcessor::saveRhythmPreset deleted — was dead code. The only call
// site (RhythmPanel::saveRhythmPreset) routes through saveRhythmPresetToFile
// instead, and the dead function had already drifted from its sibling (missing
// the modulator-child write). Removing it also resolves #432: the ch_* mixer-
// channel write that this function emitted was never read back by any load path
// (mixer settings stay attached to the slot, not the rhythm preset), so deleting
// the function removes the dead write at the same time.

juce::StringArray PresetIO::loadCategoryList() const
{
    juce::StringArray cats;
    proc_.getPresetsDir().getChildFile("categories.txt").readLines(cats);
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
    scan(proc_.getPresetsDir(), "muclid");
    scan(proc_.getRhythmsDir(), "muRhyth");
    cats.removeDuplicates(false);
    cats.removeEmptyStrings();
    cats.sort(false);
    return cats;
}

void PresetIO::ensureCategoryInList(const juce::String& cat)
{
    if (cat.isEmpty() || cat == "All" || cat == "Uncategorised") return;
    auto cats = loadCategoryList();
    if (!cats.contains(cat, false))
    {
        cats.add(cat);
        cats.sort(false);
        atomicReplaceWithText(proc_.getPresetsDir().getChildFile("categories.txt"),
                              cats.joinIntoString("\n"), proc_.onLoadError);
    }
}

void PresetIO::saveRhythmPresetToFile(int rhythmIdx, const juce::File& destFile,
                                             bool embedSample, const juce::String& category,
                                             const juce::String& description)
{
    if (rhythmIdx < 0 || rhythmIdx >= proc_.sequencer.getNumRhythms()) return;

    // if the current sample comes from an embedded-sample decode (path
    // points into %TEMP%/muClid_samples/), force-embed so we never write the
    // ephemeral temp path as `r0_sample`. The temp file would not survive an
    // OS reboot, so any later load would lose the sample.
    if (isEmbeddedSampleTempPath(proc_.loadedSamplePaths[rhythmIdx]) && ! embedSample)
    {
        embedSample = true;
        if (proc_.onLoadError)
            proc_.onLoadError("Sample originated from embedded data; saving with embed forced on.");
    }

    juce::ValueTree state("MuClidRhythm");
    state.setProperty("presetName",         destFile.getFileNameWithoutExtension(), nullptr);
    state.setProperty("presetCategory",     category,                               nullptr);
    state.setProperty("presetDescription",  description,                            nullptr);
    state.setProperty("presetEmbedSamples", embedSample ? 1 : 0,                   nullptr);
    state.setProperty("presetVersion",      kCurrentPresetVersion,                  nullptr);

    const Rhythm& r = proc_.sequencer.getRhythm(rhythmIdx);
    state.setProperty("r0_name",   juce::String(r.name),        nullptr);
    state.setProperty("r0_colour", r.colourIndex,                nullptr);
    // if we force-embedded above, we'll still write the temp path here
    // (the load path prefers `sampleData` when present and ignores `r0_sample`
    // — so the embedded copy wins), but a cleaner XML drops the stale path.
    state.setProperty("r0_sample",
                      isEmbeddedSampleTempPath(proc_.loadedSamplePaths[rhythmIdx])
                          ? juce::String()
                          : proc_.loadedSamplePaths[rhythmIdx],
                      nullptr);

    // Rhythm presets store ONLY proc_.sequencer-page state (Euclidean params, voice chain,
    // envelopes, insert effect). Mixer-page state (channel level/pan/sends/sidechain/
    // output bus) intentionally stays with the slot, not with the rhythm.
    // Stage 35: v2 writes actual values + algorithm-name strings via
    // writeParamPropertyV2. Ints / bools / algorithm selectors get their
    // natural representation in XML; floats get raw actual values.
    const juce::String srcPrefix = "r" + juce::String(rhythmIdx) + "_";
    for (int i = 0; i < kRhythmParamCount; ++i)
        if (auto* param = proc_.apvts.getParameter(srcPrefix + kRhythmParamDefs[i].suffix))
            writeParamPropertyV2(state,
                                 "r0_" + juce::String(kRhythmParamDefs[i].suffix),
                                 *param,
                                 kRhythmParamDefs[i]);

    // serialise modulators (ControlSequences + ModulationMatrix assignments).
    state.addChild(serialiseModulators(proc_.sequencer.getRhythm(rhythmIdx)), -1, nullptr);

    if (embedSample)
    {
        const juce::String path = proc_.loadedSamplePaths[rhythmIdx];
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

    atomicReplaceWithText(destFile, state.toXmlString(), proc_.onLoadError);
}

bool PresetIO::applyRhythmPreset(const juce::File& file, int targetIdx)
{
    if (!file.existsAsFile())
    {
        if (proc_.onLoadError) proc_.onLoadError("File missing: " + file.getFileName());
        return false;
    }
    auto xml = juce::parseXML(file);
    if (!xml)
    {
        if (proc_.onLoadError) proc_.onLoadError("Could not parse: " + file.getFileName());
        return false;
    }
    auto state = juce::ValueTree::fromXml(*xml);
    if (!state.isValid())
    {
        if (proc_.onLoadError) proc_.onLoadError("Invalid preset: " + file.getFileName());
        return false;
    }

    // Load only proc_.sequencer-page state — mixer settings stay attached to the slot.
    // Older rhythm preset files may include "ch_*" properties; those are simply
    // ignored on load (no read here) so legacy files load cleanly with the new policy.
    // v2-only: legacy presets refused at the entry point.
    if (! requireSupportedPresetVersion(state, file.getFileName(), proc_.onLoadError))
        return false;
    const juce::String dstPrefix = "r" + juce::String(targetIdx) + "_";
    for (int i = 0; i < kRhythmParamCount; ++i)
    {
        const auto& def = kRhythmParamDefs[i];
        const juce::String propName = "r0_" + juce::String(def.suffix);
        if (auto* param = proc_.apvts.getParameter(dstPrefix + def.suffix))
        {
            const float actualVal = readParamPropertyAsActual(state, propName, *param, def);
            if (! std::isnan(actualVal))
                param->setValueNotifyingHost(param->convertTo0to1(actualVal));
        }
    }

    Rhythm& r = proc_.sequencer.getRhythm(targetIdx);
    auto nameVal = state.getProperty("r0_name");
    if (nameVal.isString() && nameVal.toString().isNotEmpty())
        r.name = nameVal.toString().toStdString();
    r.colourIndex = (int)state.getProperty("r0_colour", r.colourIndex);

    // restore modulators if the preset carries a Modulators child.
    // Legacy .muRhyth (no Modulators) is left alone — the rhythm keeps its
    // existing in-memory CS state instead of being wiped. This is the
    // intentional opposite of clear-then-load for the host-state path: when
    // a user loads a rhythm preset that pre-dates #237, we don't want their
    // freshly-authored modulators getting destroyed.
    if (auto mods = state.getChildWithName("Modulators"); mods.isValid())
    {
        clearModulators(r);
        auto dropped = deserialiseModulators(mods, r);
        if (! dropped.isEmpty() && proc_.onLoadError)
            proc_.onLoadError("Dropped " + juce::String(dropped.size())
                        + " modulator assignment(s) from " + file.getFileName()
                        + ": " + dropped.joinIntoString("; "));
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
                proc_.voiceEngines[targetIdx]->loadFile(tmp);
                proc_.loadedSamplePaths.set(targetIdx, tmp.getFullPathName());
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
                proc_.voiceEngines[targetIdx]->loadFile(f);
                proc_.loadedSamplePaths.set(targetIdx, f.getFullPathName());
            }
            else
            {
                juce::File fallback = proc_.getSamplesDir().getChildFile(juce::File(samplePath).getFileName());
                if (fallback.existsAsFile())
                {
                    proc_.voiceEngines[targetIdx]->loadFile(fallback);
                    proc_.loadedSamplePaths.set(targetIdx, fallback.getFullPathName());
                    // warn — same-basename match in the content samples
                    // dir is not a guarantee it's the SAME file the preset
                    // originally referenced.
                    if (proc_.onLoadError)
                        proc_.onLoadError("Sample '" + juce::File(samplePath).getFileName()
                                    + "' not at original path, loaded from content folder instead.");
                }
                else
                {
                    // Linked sample missing — drop any previously loaded sample and
                    // keep the recorded path so the RhythmPanel can show a "missing"
                    // indicator and offer a relocate-to-find browse action.
                    proc_.voiceEngines[targetIdx]->clearSample();
                    proc_.loadedSamplePaths.set(targetIdx, samplePath);
                }
            }
        }
        else
        {
            proc_.voiceEngines[targetIdx]->clearSample();
            proc_.loadedSamplePaths.set(targetIdx, juce::String());
        }
    }

    // force-sync APVTS → Rhythm so freshly-defaulted slots (after a
    // shrink/grow cycle) get their fields repopulated even when JUCE skips
    // the parameterChanged listener because incoming values match the
    // already-stored APVTS values.
    proc_.forceSyncRhythmFromAPVTS(targetIdx);
    return true;
}

bool PresetIO::applyDefaultRhythm(int rhythmIndex)
{
    // route via stageRhythmPreset so it respects play state — when playing,
    // the swap is cued at the loop boundary like every other preset-load. The
    // previous direct call to applyRhythmPreset glitched audio when used mid-play
    // (sidebar "Add Rhythm" / per-rhythm reset paths). stageRhythmPreset takes
    // the immediate-apply branch when stopped, so the stopped behaviour is
    // unchanged.
    const juce::File f = proc_.getRhythmsDir().getChildFile("_default.muRhyth");
    if (!f.existsAsFile()) return false;
    stageRhythmPreset(rhythmIndex, f);
    return true;
}

void PresetIO::loadDefaultPreset()
{
    juce::File f = proc_.getPresetsDir().getChildFile("_default.muclid");
    if (f.existsAsFile())
        loadPreset(f);
}

//==============================================================================
// Modulator state serialisation.
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
// enumName, readEnumIndex, serialiseModulators, deserialiseModulators, clearModulators
// are now inline in ModulatorSerialise.h — see using declarations at top of file.


static void populateStateTree(juce::ValueTree& state, int numRhythms,
                              SequencerEngine& seq, const juce::StringArray& samplePaths)
{
    state.setProperty("formatVersion", kCurrentStateFormatVersion, nullptr);
    state.setProperty("numRhythms", numRhythms, nullptr);
    for (int i = 0; i < numRhythms; ++i)
    {
        const Rhythm& r = seq.getRhythm(i);
        state.setProperty("r" + juce::String(i) + "_name",   juce::String(r.name),   nullptr);
        state.setProperty("r" + juce::String(i) + "_colour", r.colourIndex,           nullptr);
        state.setProperty("r" + juce::String(i) + "_sample", samplePaths[i],          nullptr);

        // per-rhythm modulator state as a child of the APVTS state tree.
        auto mods = serialiseModulators(r);
        mods.setProperty("rhythmIdx", i, nullptr);
        state.addChild(mods, -1, nullptr);
    }
}

void PresetIO::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = proc_.apvts.copyState();
    populateStateTree(state, proc_.sequencer.getNumRhythms(), proc_.sequencer, proc_.loadedSamplePaths);
    juce::MemoryOutputStream(destData, true).writeString(state.toXmlString());
}

// in-place migration of pre-#217 host state. APVTS stores parameter
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

void PresetIO::restoreStateFromTree(const juce::ValueTree& state)
{
    int n = juce::jlimit(1, SequencerEngine::MaxRhythms,
                         (int)state.getProperty("numRhythms", 1));

    // Expand to MaxRhythms so parameterChanged can write to all 8 rhythm slots.
    proc_.sequencer.setNumRhythms(SequencerEngine::MaxRhythms);

    // migrate legacy state in-place before pushing it into APVTS so the
    // new 0..10 s ADSR ranges don't clamp old 0..100 values to absurd attacks.
    juce::ValueTree migrated = state.createCopy();
    migrateLegacyHostState(migrated);

    {
        mu_core::ScopedApvtsLoading guard(proc_.apvtsLoading);
        proc_.apvts.replaceState(migrated);
    }

    // Trim to actual active count.
    proc_.sequencer.setNumRhythms(n);

    // restore per-rhythm modulator state from the Modulators children.
    // Each child carries a rhythmIdx property so we apply to the right slot
    // regardless of child ordering. Legacy state (no Modulators children)
    // leaves rhythm defaults in place — clean degradation.
    for (int ci = 0; ci < state.getNumChildren(); ++ci)
    {
        auto child = state.getChild(ci);
        if (child.getType() != juce::Identifier("Modulators")) continue;
        const int ri = (int)child.getProperty("rhythmIdx", -1);
        if (ri < 0 || ri >= n) continue;
        Rhythm& target = proc_.sequencer.getRhythm(ri);
        clearModulators(target);
        auto dropped = deserialiseModulators(child, target);
        if (! dropped.isEmpty() && proc_.onLoadError)
            proc_.onLoadError("Dropped " + juce::String(dropped.size())
                        + " modulator assignment(s) on rhythm " + juce::String(ri + 1)
                        + ": " + dropped.joinIntoString("; "));
    }

    // Populate fixed voice/midi arrays to match n.
    // Ordering matters: when shrinking, store the new (smaller) count BEFORE destroying
    // excess slots so the audio thread can't access a slot being reset.  When expanding,
    // create and prepare slots BEFORE incrementing the count so the audio thread never
    // sees an uninitialised slot.
    const int oldN = proc_.numActiveRhythms.load(std::memory_order_acquire);
    if (n < oldN)
    {
        proc_.numActiveRhythms.store(n, std::memory_order_release);  // decrement first
        for (int i = n; i < oldN; ++i)
        {
            proc_.voiceEngines[i].reset();
            proc_.midiEngines[i] = MidiOutputEngine{};
        }
    }
    else
    {
        for (int i = oldN; i < n; ++i)
        {
            proc_.voiceEngines[i] = std::make_unique<VoiceEngine>();
            if (proc_.currentSampleRate > 0 && proc_.currentBlockSize > 0)
            {
                proc_.voiceEngines[i]->prepareToPlay(proc_.currentSampleRate, proc_.currentBlockSize);
                proc_.midiEngines[i].prepare(proc_.currentSampleRate, proc_.currentBlockSize);
            }
        }
        proc_.numActiveRhythms.store(n, std::memory_order_release);  // increment after slots ready
    }

    // Restore non-APVTS properties and refresh engines.
    for (int i = 0; i < n; ++i)
    {
        Rhythm& r = proc_.sequencer.getRhythm(i);
        r.name        = state.getProperty("r" + juce::String(i) + "_name",
                                          "Rhythm " + juce::String(i + 1)).toString().toStdString();
        r.colourIndex = (int)state.getProperty("r" + juce::String(i) + "_colour", i % 30);

        // force-sync APVTS → Rhythm so shrink/grow cycles (preset A → B → A)
        // repopulate freshly-defaulted Rhythm fields even when JUCE skips listener
        // callbacks because the APVTS values didn't change. Internally calls
        // updatePattern + proc_.voiceEngines[i]->setParams.
        proc_.forceSyncRhythmFromAPVTS(i);

        juce::String samplePath = state.getProperty("r" + juce::String(i) + "_sample").toString();
        juce::String sampleData = state.getProperty("r" + juce::String(i) + "_sampleData").toString();
        juce::String sampleName = state.getProperty("r" + juce::String(i) + "_sampleName").toString();

        proc_.loadedSamplePaths.set(i, samplePath);

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
                    proc_.voiceEngines[i]->loadFile(tempFile);
                    proc_.loadedSamplePaths.set(i, tempFile.getFullPathName());
                }
            }
        }
        else if (samplePath.isNotEmpty())
        {
            juce::File f(samplePath);
            if (f.existsAsFile())
            {
                proc_.voiceEngines[i]->loadFile(f);
            }
            else
            {
                juce::File fallback = proc_.getSamplesDir().getChildFile(juce::File(samplePath).getFileName());
                if (fallback.existsAsFile())
                {
                    proc_.voiceEngines[i]->loadFile(fallback);
                    proc_.loadedSamplePaths.set(i, fallback.getFullPathName());
                    // warn that we used a same-basename fallback.
                    if (proc_.onLoadError)
                        proc_.onLoadError("Sample '" + juce::File(samplePath).getFileName()
                                    + "' not at original path, loaded from content folder instead (rhythm "
                                    + juce::String(i + 1) + ").");
                }
            }
        }
    }
}

void PresetIO::setStateInformation(const void* data, int sizeInBytes)
{
    // in standalone, the user's saved `_default.muclid` is authoritative
    // on every launch — JUCE's auto-saved "filterState" should NOT override it.
    // The host (DAW) path still needs setStateInformation to restore project
    // state, so this override only fires when running standalone.
    if (proc_.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
    {
        const juce::File defaultPreset = proc_.getPresetsDir().getChildFile("_default.muclid");
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
        else if (proc_.onLoadError)
            proc_.onLoadError("Host state restore failed: invalid tree");
    }
    else if (proc_.onLoadError)
    {
        proc_.onLoadError("Host state restore failed: could not parse XML");
    }
}

//==============================================================================
void PresetIO::savePreset(const juce::String& name,
                                 const juce::String& description,
                                 const juce::String& category,
                                 bool embedSamples)
{
    const int n = proc_.sequencer.getNumRhythms();

    // if any rhythm's sample currently lives in our embedded-decode temp
    // dir, force-embed the whole preset. The temp file is ephemeral; writing
    // its path as `sample` would silently break the preset on next load.
    if (! embedSamples)
    {
        for (int i = 0; i < n; ++i)
        {
            if (isEmbeddedSampleTempPath(proc_.loadedSamplePaths[i]))
            {
                embedSamples = true;
                if (proc_.onLoadError)
                    proc_.onLoadError("One or more samples originated from embedded data; saving with embed forced on.");
                break;
            }
        }
    }

    juce::ValueTree root("MuClidPreset");
    root.setProperty("presetName",         name,                 nullptr);
    root.setProperty("presetDescription",  description,          nullptr);
    root.setProperty("presetEmbedSamples", embedSamples ? 1 : 0, nullptr);
    root.setProperty("presetCategory",    category,    nullptr);
    root.setProperty("presetVersion",     kCurrentPresetVersion, nullptr);

    for (int i = 0; i < n; ++i)
    {
        const Rhythm& r = proc_.sequencer.getRhythm(i);
        juce::ValueTree rTree("Rhythm");
        rTree.setProperty("name",   juce::String(r.name), nullptr);
        rTree.setProperty("colour", r.colourIndex,         nullptr);
        // drop the temp-dir path; the embedded sampleData child below
        // will carry the actual bytes.
        rTree.setProperty("sample",
                          isEmbeddedSampleTempPath(proc_.loadedSamplePaths[i])
                              ? juce::String()
                              : proc_.loadedSamplePaths[i],
                          nullptr);

        // Stage 35: v2 writes per the param's ParamKind — actual values for
        // floats, ints, bools as "true"/"false", algorithm selectors as the
        // stable name string.
        const juce::String srcPrefix = "r" + juce::String(i) + "_";
        for (int j = 0; j < kRhythmParamCount; ++j)
            if (auto* param = proc_.apvts.getParameter(srcPrefix + kRhythmParamDefs[j].suffix))
                writeParamPropertyV2(rTree,
                                     juce::String(kRhythmParamDefs[j].suffix),
                                     *param,
                                     kRhythmParamDefs[j]);

        const juce::String chSrcPrefix = "ch" + juce::String(i) + "_";
        for (int j = 0; kChannelSuffixes[j] != nullptr; ++j)
            if (auto* param = proc_.apvts.getParameter(chSrcPrefix + kChannelSuffixes[j]))
                rTree.setProperty("ch_" + juce::String(kChannelSuffixes[j]), param->getValue(), nullptr);

        // serialise modulators per rhythm.
        rTree.addChild(serialiseModulators(r), -1, nullptr);

        if (embedSamples)
        {
            const juce::String path = proc_.loadedSamplePaths[i];
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
    // Stage 35 + #451: v2 writes GlobalState per the ParamKind in kGlobalParamDefs.
    // eff_algo / rev_algo / mst_insChar / mst_ins2Char emit their stable algorithm
    // name string; bool params emit "true"/"false"; the rest emit actual values.
    juce::ValueTree globalTree("GlobalState");
    for (int i = 0; i < mu_pp::kGlobalParamDefCount; ++i)
    {
        const auto& def = mu_pp::kGlobalParamDefs[i];
        if (auto* param = proc_.apvts.getParameter(def.id))
        {
            const float actual = param->convertFrom0to1(param->getValue());
            writeKindedProperty(globalTree, juce::String(def.id), actual, def.kind, def.algorithmNames);
        }
    }
    root.addChild(globalTree, -1, nullptr);

    auto dir = proc_.getPresetsDir();
    dir.createDirectory();

    juce::String safeName = name.replaceCharacters("\\/:|*?<>\"", "_________");
    if (safeName.isEmpty()) safeName = "Preset";
    atomicReplaceWithText(dir.getChildFile(safeName + ".muclid"), root.toXmlString(), proc_.onLoadError);
}

void PresetIO::loadPreset(const juce::File& file)
{
    auto xml = juce::parseXML(file);
    if (!xml)
    {
        if (proc_.onLoadError) proc_.onLoadError("Could not parse: " + file.getFileName());
        return;
    }
    auto root = juce::ValueTree::fromXml(*xml);
    if (!root.isValid())
    {
        if (proc_.onLoadError) proc_.onLoadError("Invalid preset: " + file.getFileName());
        return;
    }

    if (root.getType() == juce::Identifier("MuClidPreset"))
    {
        // Reject legacy formats BEFORE mutating proc_.sequencer state. Otherwise a v0
        // preset wipes the user's existing rhythms during the resize block below
        // and only then reports the rejection — leaving the project in a
        // half-loaded default state instead of the pre-load state.
        if (! requireSupportedPresetVersion(root, file.getFileName(), proc_.onLoadError))
            return;

        // Count only Rhythm-type children; GlobalState child was added in #123.
        int n = 0;
        for (int ci = 0; ci < root.getNumChildren(); ++ci)
            if (root.getChild(ci).getType() == juce::Identifier("Rhythm"))
                ++n;
        n = juce::jlimit(1, SequencerEngine::MaxRhythms, n);
        proc_.sequencer.setNumRhythms(n);

        const int oldN2 = proc_.numActiveRhythms.load(std::memory_order_acquire);
        if (n < oldN2)
        {
            proc_.numActiveRhythms.store(n, std::memory_order_release);
            for (int i = n; i < oldN2; ++i)
            {
                proc_.voiceEngines[i].reset();
                proc_.midiEngines[i] = MidiOutputEngine{};
                // Wipe mixer channel state and sample-path memory so the now-inactive
                // slot does not retain stale fader/sidechain/sample data from the
                // pre-load session. Without this, opening the mixer after loading a
                // smaller preset would still show the previous tenant's settings on
                // the dimmed slots.
                proc_.mixerEngine.channels[i] = MixerEngine::ChannelState{};
                if (i < proc_.loadedSamplePaths.size())
                    proc_.loadedSamplePaths.set(i, juce::String());
            }
        }
        else
        {
            for (int i = oldN2; i < n; ++i)
            {
                proc_.voiceEngines[i] = std::make_unique<VoiceEngine>();
                if (proc_.currentSampleRate > 0 && proc_.currentBlockSize > 0)
                {
                    proc_.voiceEngines[i]->prepareToPlay(proc_.currentSampleRate, proc_.currentBlockSize);
                    proc_.midiEngines[i].prepare(proc_.currentSampleRate, proc_.currentBlockSize);
                }
            }
            proc_.numActiveRhythms.store(n, std::memory_order_release);
        }

        mu_core::ScopedApvtsLoading guard(proc_.apvtsLoading);

        int rhythmIdx = 0;
        for (int ci = 0; ci < root.getNumChildren() && rhythmIdx < n; ++ci)
        {
            auto rTree = root.getChild(ci);
            if (rTree.getType() != juce::Identifier("Rhythm")) continue;
            const int i = rhythmIdx++;

            const juce::String dstPrefix = "r" + juce::String(i) + "_";
            for (int j = 0; j < kRhythmParamCount; ++j)
            {
                const auto& def = kRhythmParamDefs[j];
                if (auto* param = proc_.apvts.getParameter(dstPrefix + def.suffix))
                {
                    const float actualVal = readParamPropertyAsActual(rTree, juce::String(def.suffix), *param, def);
                    if (! std::isnan(actualVal))
                        param->setValueNotifyingHost(param->convertTo0to1(actualVal));
                }
            }

            const juce::String dstChPrefix = "ch" + juce::String(i) + "_";
            for (int j = 0; kChannelSuffixes[j] != nullptr; ++j)
            {
                juce::Identifier chPropId { "ch_" + juce::String(kChannelSuffixes[j]) };
                if (rTree.hasProperty(chPropId))
                    if (auto* param = proc_.apvts.getParameter(dstChPrefix + kChannelSuffixes[j]))
                        param->setValueNotifyingHost((float)rTree.getProperty(chPropId));
            }

            Rhythm& r = proc_.sequencer.getRhythm(i);

            // restore modulators if the preset carries a Modulators child.
            // Same opt-in behaviour as applyRhythmPreset — legacy .muclid files
            // (no Modulators child per rhythm) leave the rhythm's existing
            // in-memory state in place, avoiding accidental destruction.
            if (auto rMods = rTree.getChildWithName("Modulators"); rMods.isValid())
            {
                clearModulators(r);
                auto dropped = deserialiseModulators(rMods, r);
                if (! dropped.isEmpty() && proc_.onLoadError)
                    proc_.onLoadError("Dropped " + juce::String(dropped.size())
                                + " modulator assignment(s) on rhythm " + juce::String(i + 1)
                                + ": " + dropped.joinIntoString("; "));
            }

            auto nameVal = rTree.getProperty("name");
            if (nameVal.isString() && nameVal.toString().isNotEmpty())
                r.name = nameVal.toString().toStdString();
            r.colourIndex = (int)rTree.getProperty("colour", r.colourIndex);

            juce::String sampleData = rTree.getProperty("sampleData").toString();
            juce::String sampleName = rTree.getProperty("sampleName").toString();
            juce::String samplePath = rTree.getProperty("sample").toString();
            proc_.loadedSamplePaths.set(i, samplePath);

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
                        proc_.voiceEngines[i]->loadFile(tempFile);
                        proc_.loadedSamplePaths.set(i, tempFile.getFullPathName());
                    }
                }
            }
            else if (samplePath.isNotEmpty())
            {
                juce::File f(samplePath);
                if (f.existsAsFile())
                {
                    proc_.voiceEngines[i]->loadFile(f);
                }
                else
                {
                    juce::File fallback = proc_.getSamplesDir().getChildFile(juce::File(samplePath).getFileName());
                    if (fallback.existsAsFile())
                    {
                        proc_.voiceEngines[i]->loadFile(fallback);
                        proc_.loadedSamplePaths.set(i, fallback.getFullPathName());
                        // warn about the same-basename fallback.
                        if (proc_.onLoadError)
                            proc_.onLoadError("Sample '" + juce::File(samplePath).getFileName()
                                        + "' not at original path, loaded from content folder instead (rhythm "
                                        + juce::String(i + 1) + ").");
                    }
                    else
                    {
                        // Linked sample not found — clear any previously loaded sample
                        // on this slot so the RhythmPanel shows the missing indicator
                        // consistently and the user can click to relocate.
                        proc_.voiceEngines[i]->clearSample();
                        // proc_.loadedSamplePaths already set to samplePath above (line ~1999)
                    }
                }
            }
            else
            {
                // Preset rhythm has no sample at all — wipe any stale sample on this slot.
                proc_.voiceEngines[i]->clearSample();
            }

            // force-sync APVTS → Rhythm so a preset that re-loads identical
            // values into a freshly-recreated slot (preset A → B → A pattern)
            // still populates r.voiceParams / r.genA.hits. JUCE skips listener
            // callbacks when setValueNotifyingHost is called with an unchanged
            // value, so parameterChanged → syncRhythmParam never fires.
            // Internally calls updatePattern + proc_.voiceEngines[i]->setParams.
            proc_.forceSyncRhythmFromAPVTS(i);
        }

        // Restore global FX/mixer state if present (added in #123; older files omit this).
        // Stage 35 + #451: iterate kGlobalParamDefs so algorithm-selector params
        // (eff_algo / rev_algo / mst_insChar / mst_ins2Char) get name-string
        // lookup in v2. v0/v1 paths stay normalised with #430 migration for v0.
        for (int ci = 0; ci < root.getNumChildren(); ++ci)
        {
            auto child = root.getChild(ci);
            if (child.getType() != juce::Identifier("GlobalState")) continue;
            for (int gi = 0; gi < mu_pp::kGlobalParamDefCount; ++gi)
            {
                const auto& def = mu_pp::kGlobalParamDefs[gi];
                if (auto* param = proc_.apvts.getParameter(def.id))
                {
                    const float actualVal = readGlobalPropertyAsActual(child, juce::String(def.id),
                                                                       *param, def);
                    if (! std::isnan(actualVal))
                        param->setValueNotifyingHost(param->convertTo0to1(actualVal));
                }
            }
            break;
        }
        // ScopedApvtsLoading guard goes out of scope at the closing brace below.
    }
    else
    {
        restoreStateFromTree(root);
    }
}
