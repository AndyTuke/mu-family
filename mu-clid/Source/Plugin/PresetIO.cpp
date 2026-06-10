#include "PresetIO.h"
#include "PluginProcessor.h"
#include "PluginProcessor_Internal.h"
#include "Persistence/PresetHelpers.h"      // writeKindedProperty, readKindedPropertyAsActualV2, kGlobalParamDefs
#include "Persistence/PresetMigrations.h"   // v3 insert/master/mod-assignment migrations (#664)
#include "Persistence/ModulatorSerialise.h" // serialiseModulators, deserialiseModulators, clearModulators
#include "UI/Components/MuLookAndFeel.h" // kChannelPaletteSize
#include <limits>               // std::numeric_limits for NaN sentinel

using mu_pp::kRhythmParamDefs;
using mu_pp::kRhythmParamCount;
using mu_pp::kChannelSuffixes;
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
using mu_pp_migrate::migrateInsertSlotsV3;       // moved to PresetMigrations (#664)
using mu_pp_migrate::migrateMasterInsertSlotsV3;
using mu_pp_migrate::migrateModAssignmentsV3;

// serialiseModulators / deserialiseModulators / clearModulators are defined
// as inline functions in ModulatorSerialise.h (brought in via the using
// declarations above), so no forward declarations are needed here.

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

// Preset version gating removed — pre-distribution era, every preset just needs to work
// with the current build (#643). Migration helpers (migrateInsertSlotsV3 etc.) still run
// unconditionally inside the load path so older v0/v1/v2 properties still get translated;
// any preset that survives those migrations loads fine. The unused fileName + onLoadError
// params are kept on the signature so callers don't need restructuring.
static bool requireSupportedPresetVersion(const juce::ValueTree& /*tree*/,
                                          const juce::String& /*fileName*/,
                                          const std::function<void(const juce::String&)>& /*onLoadError*/)
{
    return true;
}

// detect a sample path that points into our embedded-sample decode temp
// dir (%TEMP%/muClid_samples/). These paths come from loading a preset whose
// `sampleData` was base64-decoded into a temp file by the load path; they are
// NOT durable references — the temp dir gets wiped between OS sessions, and
// any subsequent .muRhythm / .muClid save that records the temp path as
// `r0_sample` would silently break on next load. Used by the save flow below
// to force-embed instead of writing the temp path.
static bool isEmbeddedSampleTempPath(const juce::String& path)
{
    if (path.isEmpty()) return false;
    const juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .getChildFile("muClid_samples");
    return tempDir.exists() && juce::File(path).isAChildOf(tempDir);
}

// Resolve a stored sample path to a juce::File to load. Relative paths
// (no drive letter / leading separator) are resolved against samplesDir so
// shipped presets with "kick.wav" find the file without a warning. Absolute
// paths are returned as-is; the caller's fallback logic handles "not found".
static juce::File resolveSamplePath(const juce::String& storedPath, const juce::File& samplesDir)
{
    if (storedPath.isEmpty()) return {};
    if (!juce::File::isAbsolutePath(storedPath))
        return samplesDir.getChildFile(storedPath);
    return juce::File(storedPath);
}

// When the sample path is inside the Samples content folder, return a path
// relative to that folder (e.g. "kicks/kick.wav") so presets are
// machine-agnostic. Otherwise return absPath unchanged.
static juce::String toRelativeSamplePath(const juce::String& absPath, const juce::File& samplesDir)
{
    if (absPath.isEmpty()) return absPath;
    juce::File f(absPath);
    if (f.isAChildOf(samplesDir))
        return f.getRelativePathFrom(samplesDir);
    return absPath;
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

    // Stage 36 v3 migration: collapse old named insert fields → insP1..4
    // (algo-aware, runs BEFORE the field-apply loop so the new fields are
    // present when restoreRhythmAPVTSParams iterates kRhythmParamDefs).
    migrateInsertSlotsV3(state, "r0_");

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

    // Name and colour. Prefer presetName (matches dropdown / filename) over
    // r0_name — older presets carry a short historical r0_name (e.g. "Kick"
    // inside "Kick Accents.muRhythm") which loses the user-facing context.
    auto presetNameVal = state.getProperty("presetName");
    auto rhythmNameVal = state.getProperty("r0_name");
    if (presetNameVal.isString() && presetNameVal.toString().isNotEmpty())
        newRhythm.name = presetNameVal.toString().toStdString();
    else if (rhythmNameVal.isString() && rhythmNameVal.toString().isNotEmpty())
        newRhythm.name = rhythmNameVal.toString().toStdString();
    newRhythm.colourIndex = (int)state.getProperty("r0_colour", newRhythm.colourIndex);

    // deserialise modulators from the preset. Mirrors the applyRhythmPreset
    // (stopped-state) path so hot-swap preset loads carry the preset's LFOs / step
    // sequences / matrix assignments instead of inheriting the previous rhythm's.
    // Stage 36: translate old `insert.drive` / `ks.note` / `voc.*` destination IDs
    // to the new `insert.p1`..`insert.p4` slots before deserialiseModulators
    // validates them (otherwise they'd be silently dropped as unknown destinations).
    if (auto mods = state.getChildWithName("Modulators"); mods.isValid())
    {
        const int algoIdxForMig = mu_audio::indexFromName(
            mu_audio::kInsertAlgorithmNames,
            state.getProperty("r0_drvChar").toString());
        migrateModAssignmentsV3(mods, algoIdxForMig);

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
            const juce::File samplesDir = proc_.getSamplesDir();
            juce::File sf = resolveSamplePath(storedPath, samplesDir);
            if (sf.existsAsFile())
            {
                newVoice->loadFile(sf);
                samplePath = sf.getFullPathName();
            }
            else if (juce::File::isAbsolutePath(storedPath))
            {
                // Absolute path failed — try filename-only fallback in Samples folder.
                juce::File fallback = samplesDir.getChildFile(sf.getFileName());
                if (fallback.existsAsFile())
                {
                    newVoice->loadFile(fallback);
                    samplePath = fallback.getFullPathName();
                    if (proc_.onLoadError)
                        proc_.onLoadError("Sample '" + sf.getFileName()
                                    + "' not at original path, loaded from content folder instead.");
                }
                else
                {
                    samplePath = storedPath;
                    if (proc_.onLoadError)
                        proc_.onLoadError("Sample '" + sf.getFileName()
                                    + "' missing — rhythm loaded without audio.");
                }
            }
            else
            {
                // Relative path, not found in Samples folder.
                samplePath = sf.getFullPathName();
                if (proc_.onLoadError)
                    proc_.onLoadError("Sample '" + sf.getFileName()
                                + "' missing from content folder — rhythm loaded without audio.");
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
    // Also scan .muClid and .muRhythm files for categories not yet in the list.
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
    scan(proc_.getPresetsDir(), "muClid");
    scan(proc_.getRhythmsDir(), "muRhythm");
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
    // presetVersion property dropped (#643): not distributing yet, current-build-only.

    const Rhythm& r = proc_.sequencer.getRhythm(rhythmIdx);
    state.setProperty("r0_name",   juce::String(r.name),        nullptr);
    state.setProperty("r0_colour", r.colourIndex,                nullptr);
    state.setProperty("r0_sample",
                      embedSample
                          ? juce::String()
                          : toRelativeSamplePath(proc_.loadedSamplePaths[rhythmIdx], proc_.getSamplesDir()),
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
    // v2-only: legacy presets refused at the entry point.
    if (! requireSupportedPresetVersion(state, file.getFileName(), proc_.onLoadError))
        return false;

    // Load only sequencer-page state — mixer settings stay attached to the slot.
    // Older rhythm preset files may include "ch_*" properties; those are simply
    // ignored (we don't call restoreRhythmChannelParams here).
    restoreRhythmAPVTSParams (targetIdx, state, /*srcPropPrefix*/ "r0_");
    restoreRhythmModulators  (targetIdx, state);

    // Name + colour (not APVTS-backed). For a per-rhythm preset, the
    // user-facing identity is the `presetName` (matches the dropdown entry
    // and the filename); the older `r0_name` carries the slot's short
    // historical name (e.g. "Kick" inside the "Kick Accents" preset).
    // Prefer presetName, fall back to r0_name when the file is missing one.
    Rhythm& r = proc_.sequencer.getRhythm(targetIdx);
    auto presetNameVal = state.getProperty("presetName");
    auto rhythmNameVal = state.getProperty("r0_name");
    if (presetNameVal.isString() && presetNameVal.toString().isNotEmpty())
        r.name = presetNameVal.toString().toStdString();
    else if (rhythmNameVal.isString() && rhythmNameVal.toString().isNotEmpty())
        r.name = rhythmNameVal.toString().toStdString();
    r.colourIndex = (int)state.getProperty("r0_colour", r.colourIndex);

    // In .muRhythm the sample *path* is "r0_sample" but the embedded blob fields
    // are unprefixed ("sampleData" / "sampleName").
    restoreRhythmSample(targetIdx, state, "r0_sample", "sampleData", "sampleName");

    // force-sync APVTS → Rhythm so freshly-defaulted slots (after a shrink/grow
    // cycle) get their fields repopulated even when JUCE skips the
    // parameterChanged listener because incoming values match the already-
    // stored APVTS values.
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
    const juce::File f = proc_.getRhythmsDir().getChildFile("_default.muRhythm");
    if (!f.existsAsFile()) return false;

    // "Default rhythm" is a sequencer / voice settings reset — NOT an identity
    // change. Colour is per-slot identity (cyclic next-in-palette assigned by
    // PluginEditor when the slot is added). Save the colour, apply preset,
    // restore the colour so a user-saved _default.muRhythm carrying an
    // r0_colour attribute doesn't clobber the slot's intended visual identity.
    // Stopped path is synchronous — restore immediately. Playing path stages
    // into a pending Rhythm copy; the commit later replaces the live rhythm
    // wholesale, so the restore needs to happen via the swap commit hook in
    // PluginProcessor — see onRhythmHotSwapCommitted handling there.
    const int savedColour = proc_.sequencer.getRhythm(rhythmIndex).colourIndex;
    const bool wasPlaying = proc_.sequencerPlaying.load();
    stageRhythmPreset(rhythmIndex, f);
    if (! wasPlaying)
        proc_.sequencer.getRhythm(rhythmIndex).colourIndex = savedColour;
    return true;
}

void PresetIO::loadDefaultPreset()
{
    juce::File f = proc_.getPresetsDir().getChildFile("_default.muClid");
    if (f.existsAsFile())
    {
        loadPreset(f);
        return;
    }
    // Fall back to a single-rhythm `_default.muRhythm` if the full-session default
    // isn't present. Used by the listening-test pipeline to set a known starting
    // state on standalone launch without needing a complete `.muClid` file.
    const juce::File rhy = proc_.getRhythmsDir().getChildFile("_default.muRhythm");
    if (rhy.existsAsFile() && proc_.getNumRhythms() > 0)
        applyDefaultRhythm(0);
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
    // presetVersion property dropped (#643): not distributing yet, current-build-only.

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
                              : toRelativeSamplePath(proc_.loadedSamplePaths[i], proc_.getSamplesDir()),
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
    atomicReplaceWithText(dir.getChildFile(safeName + ".muClid"), root.toXmlString(), proc_.onLoadError);
}

// ── PresetIO::loadPreset helpers ────────────────────────────────────────────
//
// loadPreset itself is a sequence of named steps; each step lives in one of the
// helpers below. All run on the message thread under a single
// mu_core::ScopedApvtsLoading guard scoped to the caller.

void PresetIO::resizeRhythmArrays(int n)
{
    proc_.sequencer.setNumRhythms(n);

    const int oldN = proc_.numActiveRhythms.load(std::memory_order_acquire);
    if (n < oldN)
    {
        proc_.numActiveRhythms.store(n, std::memory_order_release);
        for (int i = n; i < oldN; ++i)
        {
            proc_.voiceEngines[i].reset();
            proc_.midiEngines[i] = MidiOutputEngine{};
            // Wipe mixer channel state and sample-path memory so the now-inactive
            // slot doesn't retain stale fader/sidechain/sample data from the
            // pre-load session.
            proc_.mixerEngine.channels[i].reset();
            if (i < proc_.loadedSamplePaths.size())
                proc_.loadedSamplePaths.set(i, juce::String());
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
        proc_.numActiveRhythms.store(n, std::memory_order_release);
    }
}

void PresetIO::restoreRhythmAPVTSParams(int apvtsSlot, const juce::ValueTree& rTree,
                                         const juce::String& srcPropPrefix)
{
    // Stage 36 v3 migration runs on a copy so the caller's tree is untouched.
    // Order matters: v3 insert-slot migration consumes the old field names that
    // the v2 EQ-encoding migration also touches, so the order doesn't matter
    // (each is a no-op if the new slots are already present).
    juce::ValueTree migrated = rTree.createCopy();
    migrateInsertSlotsV3(migrated, srcPropPrefix);

    const juce::String dstPrefix = "r" + juce::String(apvtsSlot) + "_";
    for (int j = 0; j < kRhythmParamCount; ++j)
    {
        const auto& def = kRhythmParamDefs[j];
        if (auto* param = proc_.apvts.getParameter(dstPrefix + def.suffix))
        {
            const float actualVal = readParamPropertyAsActual(migrated, srcPropPrefix + def.suffix, *param, def);
            if (! std::isnan(actualVal))
                param->setValueNotifyingHost(param->convertTo0to1(actualVal));
        }
    }
}

void PresetIO::restoreRhythmChannelParams(int apvtsSlot, const juce::ValueTree& rTree)
{
    const juce::String dstChPrefix = "ch" + juce::String(apvtsSlot) + "_";
    for (int j = 0; kChannelSuffixes[j] != nullptr; ++j)
    {
        juce::Identifier chPropId { "ch_" + juce::String(kChannelSuffixes[j]) };
        if (rTree.hasProperty(chPropId))
            if (auto* param = proc_.apvts.getParameter(dstChPrefix + kChannelSuffixes[j]))
                param->setValueNotifyingHost((float)rTree.getProperty(chPropId));
    }
}

void PresetIO::restoreRhythmModulators(int i, const juce::ValueTree& rTree)
{
    // Opt-in: legacy .muClid files (no Modulators child per rhythm) leave the
    // rhythm's existing in-memory state in place — avoids accidental destruction.
    auto rMods = rTree.getChildWithName("Modulators");
    if (! rMods.isValid()) return;

    // Stage 36: translate old insert.* / ks.* / voc.* destination IDs to the
    // new insert.p1..p4 slots BEFORE deserialiseModulators validates them.
    // Algorithm-aware — drvChar in the rTree (or empty fallback in the full
    // MuClidPreset where Rhythm children don't carry the prefix).
    juce::ValueTree migratedMods = rMods.createCopy();
    const juce::String charProp = rTree.hasProperty("drvChar") ? "drvChar"
                                                                : "r0_drvChar";
    const int algoIdxForMig = mu_audio::indexFromName(
        mu_audio::kInsertAlgorithmNames,
        rTree.getProperty(charProp).toString());
    migrateModAssignmentsV3(migratedMods, algoIdxForMig);

    Rhythm& r = proc_.sequencer.getRhythm(i);
    clearModulators(r);
    auto dropped = deserialiseModulators(migratedMods, r);
    if (! dropped.isEmpty() && proc_.onLoadError)
        proc_.onLoadError("Dropped " + juce::String(dropped.size())
                    + " modulator assignment(s) on rhythm " + juce::String(i + 1)
                    + ": " + dropped.joinIntoString("; "));
}

void PresetIO::restoreRhythmSample(int i, const juce::ValueTree& tree,
                                    const juce::String& samplePathProp,
                                    const juce::String& sampleDataProp,
                                    const juce::String& sampleNameProp)
{
    // The Lite (MIDI-effect) build has no sample-playback engine on this slot, so there's
    // nothing to load a sample into — bail before any voiceEngines[i] deref. The caller
    // maintains loadedSamplePaths. (Mirrors the null guard in forceSyncRhythmFromAPVTS.)
    if (! proc_.voiceEngines[i]) return;

    const juce::String sampleData = tree.getProperty(juce::Identifier(sampleDataProp)).toString();
    const juce::String sampleName = tree.getProperty(juce::Identifier(sampleNameProp)).toString();
    const juce::String samplePath = tree.getProperty(juce::Identifier(samplePathProp)).toString();

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
        return;
    }

    if (samplePath.isEmpty())
    {
        // Preset rhythm has no sample at all — wipe any stale sample on this slot.
        proc_.voiceEngines[i]->clearSample();
        proc_.loadedSamplePaths.set(i, juce::String());
        return;
    }

    const juce::File samplesDir = proc_.getSamplesDir();
    juce::File f = resolveSamplePath(samplePath, samplesDir);
    if (f.existsAsFile())
    {
        proc_.voiceEngines[i]->loadFile(f);
        proc_.loadedSamplePaths.set(i, f.getFullPathName());
    }
    else if (juce::File::isAbsolutePath(samplePath))
    {
        juce::File fallback = samplesDir.getChildFile(f.getFileName());
        if (fallback.existsAsFile())
        {
            proc_.voiceEngines[i]->loadFile(fallback);
            proc_.loadedSamplePaths.set(i, fallback.getFullPathName());
            if (proc_.onLoadError)
                proc_.onLoadError("Sample '" + f.getFileName()
                            + "' not at original path, loaded from content folder instead (rhythm "
                            + juce::String(i + 1) + ").");
        }
        else
        {
            proc_.voiceEngines[i]->clearSample();
            proc_.loadedSamplePaths.set(i, samplePath);
        }
    }
    else
    {
        // Relative path, not found in Samples folder.
        proc_.voiceEngines[i]->clearSample();
        proc_.loadedSamplePaths.set(i, f.getFullPathName());
    }
}

void PresetIO::restoreGlobalState(const juce::ValueTree& root)
{
    // GlobalState child was added in #123; older files omit this and the loop
    // simply does nothing. Stage 35 + #451: iterate kGlobalParamDefs so algorithm-
    // selector params (eff_algo / rev_algo / mst_insChar / mst_ins2Char) use the
    // name-string lookup path in v2.
    for (int ci = 0; ci < root.getNumChildren(); ++ci)
    {
        auto child = root.getChild(ci);
        if (child.getType() != juce::Identifier("GlobalState")) continue;

        // Stage 36 v3 migration: collapse master-insert named fields →
        // insP1..4 (algo-aware). Mutates a copy so original child is untouched.
        juce::ValueTree migrated = child.createCopy();
        migrateMasterInsertSlotsV3(migrated, 1);
        migrateMasterInsertSlotsV3(migrated, 2);

        for (int gi = 0; gi < mu_pp::kGlobalParamDefCount; ++gi)
        {
            const auto& def = mu_pp::kGlobalParamDefs[gi];
            if (auto* param = proc_.apvts.getParameter(def.id))
            {
                const float actualVal = readGlobalPropertyAsActual(migrated, juce::String(def.id),
                                                                   *param, def);
                if (! std::isnan(actualVal))
                    param->setValueNotifyingHost(param->convertTo0to1(actualVal));
            }
        }
        return;
    }
}

// ── Prestaging (playing path) ───────────────────────────────────────────────
//
// Build a fully-prepared Rhythm + VoiceEngine from one .muClid <Rhythm> child,
// entirely off the audio thread. Mirrors the per-rhythm hot-swap build in
// stageRhythmPreset, but starts from a default Rhythm and reads the unprefixed
// (.muClid) property names. The expensive bits — sample decode / disk load and
// VoiceEngine::prepareToPlay — happen here at stage time so the loop-boundary
// commit is glitch-free.
static void prepareRhythmSlotFromTree(const juce::ValueTree& rTreeIn,
                                      double sampleRate, int blockSize,
                                      const juce::File& samplesDir,
                                      const std::function<void(const juce::String&)>& onLoadError,
                                      Rhythm& outRhythm,
                                      std::unique_ptr<VoiceEngine>& outVoice,
                                      juce::String& outSamplePath)
{
    Rhythm r;  // default; preset values applied on top

    // v3 insert-slot migration (algo-aware) on a copy. .muClid Rhythm children use
    // unprefixed property names, so srcPrefix is "".
    juce::ValueTree rTree = rTreeIn.createCopy();
    migrateInsertSlotsV3(rTree, "");

    for (int j = 0; j < kRhythmParamCount; ++j)
    {
        const auto& def = kRhythmParamDefs[j];
        const juce::String propName = juce::String(def.suffix);
        if (! rTree.hasProperty(propName)) continue;
        const float actualVal = readKindedPropertyAsActualV2(rTree, propName, def.kind, def.algorithmNames);
        if (std::isnan(actualVal)) continue;
        bool pd = false, vd = false;
        applyRhythmSuffix(def.suffix, actualVal, r, pd, vd);
    }

    // Name + colour (Rhythm-struct fields, not APVTS-backed).
    auto nameVal = rTree.getProperty("name");
    if (nameVal.isString() && nameVal.toString().isNotEmpty())
        r.name = nameVal.toString().toStdString();
    r.colourIndex = (int) rTree.getProperty("colour", r.colourIndex);

    // Modulators. Translate legacy destination IDs before deserialising, same as
    // the per-rhythm hot-swap path.
    if (auto mods = rTree.getChildWithName("Modulators"); mods.isValid())
    {
        const int algoIdx = mu_audio::indexFromName(mu_audio::kInsertAlgorithmNames,
                                                    rTree.getProperty("drvChar").toString());
        migrateModAssignmentsV3(mods, algoIdx);
        clearModulators(r);
        auto dropped = deserialiseModulators(mods, r);
        if (! dropped.isEmpty() && onLoadError)
            onLoadError("Dropped " + juce::String(dropped.size()) + " modulator assignment(s).");
    }

    // Build + prime the voice engine off the audio thread.
    outVoice = std::make_unique<VoiceEngine>();
    outVoice->prepareToPlay(sampleRate, blockSize);
    outVoice->setParams(r.voiceParams);

    // Sample: embedded blob takes priority, else stored path (mirrors restoreRhythmSample).
    const juce::String sampleData = rTree.getProperty("sampleData").toString();
    const juce::String sampleName = rTree.getProperty("sampleName").toString();
    if (sampleData.isNotEmpty() && sampleName.isNotEmpty())
    {
        juce::MemoryBlock mb;
        { juce::MemoryOutputStream mos(mb, false); juce::Base64::convertFromBase64(mos, sampleData); }
        if (mb.getSize() > 0)
        {
            juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                     .getChildFile("muClid_samples");
            tempDir.createDirectory();
            juce::File tempFile = tempDir.getChildFile(sampleName);
            if (tempFile.replaceWithData(mb.getData(), mb.getSize()))
            {
                outVoice->loadFile(tempFile);
                outSamplePath = tempFile.getFullPathName();
            }
        }
    }
    else
    {
        const juce::String storedPath = rTree.getProperty("sample").toString();
        if (storedPath.isEmpty())
        {
            outVoice->clearSample();
        }
        else
        {
            juce::File f = resolveSamplePath(storedPath, samplesDir);
            if (f.existsAsFile())
            {
                outVoice->loadFile(f);
                outSamplePath = f.getFullPathName();
            }
            else if (juce::File::isAbsolutePath(storedPath))
            {
                juce::File fallback = samplesDir.getChildFile(f.getFileName());
                if (fallback.existsAsFile())
                {
                    outVoice->loadFile(fallback);
                    outSamplePath = fallback.getFullPathName();
                    if (onLoadError)
                        onLoadError("Sample '" + f.getFileName()
                                    + "' not at original path, loaded from content folder instead.");
                }
                else
                {
                    outVoice->clearSample();
                    outSamplePath = storedPath;
                    if (onLoadError)
                        onLoadError("Sample '" + f.getFileName() + "' missing — rhythm loaded without audio.");
                }
            }
            else
            {
                outVoice->clearSample();
                outSamplePath = f.getFullPathName();
                if (onLoadError)
                    onLoadError("Sample '" + f.getFileName()
                                + "' missing from content folder — rhythm loaded without audio.");
            }
        }
    }

    outRhythm = std::move(r);
}

// Build the whole prepared payload from a parsed MuClidPreset root.
static HotSwapStager::PreparedFullPreset
buildPreparedFullPreset(const juce::ValueTree& root, double sampleRate, int blockSize,
                        const juce::File& samplesDir,
                        const std::function<void(const juce::String&)>& onLoadError)
{
    HotSwapStager::PreparedFullPreset out;
    out.tree = root;

    int n = 0;
    for (int ci = 0; ci < root.getNumChildren(); ++ci)
        if (root.getChild(ci).getType() == juce::Identifier("Rhythm")) ++n;
    n = juce::jlimit(1, SequencerEngine::MaxRhythms, n);
    out.numRhythms = n;

    int idx = 0;
    for (int ci = 0; ci < root.getNumChildren() && idx < n; ++ci)
    {
        auto rTree = root.getChild(ci);
        if (rTree.getType() != juce::Identifier("Rhythm")) continue;
        const int i = idx++;
        prepareRhythmSlotFromTree(rTree, sampleRate, blockSize, samplesDir, onLoadError,
                                  out.rhythms[(size_t) i], out.voices[(size_t) i],
                                  out.samplePaths[(size_t) i]);
    }
    return out;
}

void PresetIO::loadPreset(const juce::File& file)
{
    auto xml = juce::parseXML(file);
    if (! xml)
    {
        if (proc_.onLoadError) proc_.onLoadError("Could not parse: " + file.getFileName());
        return;
    }
    auto root = juce::ValueTree::fromXml(*xml);
    if (! root.isValid())
    {
        if (proc_.onLoadError) proc_.onLoadError("Invalid preset: " + file.getFileName());
        return;
    }

    // A non-MuClidPreset root is host / project state (the getStateInformation
    // format), not a .muclid preset — restore it directly.
    if (root.getType() != juce::Identifier("MuClidPreset"))
    {
        restoreStateFromTree(root);
        return;
    }

    // Full .muclid preset — ONE unified path. Pre-build every Rhythm + VoiceEngine +
    // sample off the audio thread (buildPreparedFullPreset), then just trigger the
    // switch: when playing, defer the flip to the next loop point (stageFullPreset →
    // boundary commit); when stopped, flip immediately (commitStagedFullPreset). Same
    // build + same commit code — only the trigger differs, so stopped and playing loads
    // are identical by construction (no divergent second path — #666) and the stopped
    // load is glitch-free with its sample disk I/O done off the rhythmsLock (#663).
    auto prepared = buildPreparedFullPreset(root, proc_.currentSampleRate,
                                            proc_.currentBlockSize, proc_.getSamplesDir(),
                                            proc_.onLoadError);
    if (proc_.sequencerPlaying.load())
        proc_.hotSwapStager.stageFullPreset(std::move(prepared));
    else
        commitStagedFullPreset(prepared);
}

//==============================================================================
void PresetIO::commitStagedFullPreset(HotSwapStager::PreparedFullPreset& prepared)
{
    const int n    = prepared.numRhythms;
    const int oldN = proc_.numActiveRhythms.load(std::memory_order_acquire);

    // ── Install the pre-built voices + rhythms under suspend + rhythmsLock ─────
    // suspendProcessing stops FUTURE processBlock calls; rhythmsLock serialises with
    // any IN-FLIGHT one (#663 — suspend alone doesn't block it). Everything here is
    // in-memory (no parse, no disk I/O — done at stage time), so the lock is held for
    // microseconds and the swap stays glitch-free. The lock is released before the
    // APVTS finalize below so that (post-resume) work doesn't bail the audio thread.
    proc_.suspendProcessing(true);
    {
        const juce::ScopedLock sl(proc_.rhythmsLock);

    proc_.sequencer.setNumRhythms(n);

    if (n < oldN)
        proc_.numActiveRhythms.store(n, std::memory_order_release);  // shrink: drop count first

    for (int i = 0; i < n; ++i)
    {
        // Retire-then-swap (mirrors HotSwapStager::processSwaps): the outgoing
        // engine is parked in a retired slot + marked released so it keeps
        // rendering its sample tail / amp-envelope release while the new preset
        // plays, instead of being hard-cut. Grown slots have no old engine.
        auto oldEngine = std::move(proc_.voiceEngines[(size_t) i]);
        proc_.voiceEngines[(size_t) i] = std::move(prepared.voices[(size_t) i]);
        if (oldEngine)
        {
            oldEngine->markRetired();
            bool placed = false;
            for (auto& slot : proc_.retiredVoiceEngines[(size_t) i])
                if (! slot) { slot = std::move(oldEngine); placed = true; break; }
            if (! placed)
            {
                // All retired slots full — force-cut slot 0 (spam-swap back-pressure).
                proc_.retiredVoiceEngines[(size_t) i][0] = std::move(oldEngine);
                proc_.retiredReadyForCleanup[(size_t) i][0].store(false, std::memory_order_release);
            }
        }

        proc_.sequencer.getRhythm(i) = std::move(prepared.rhythms[(size_t) i]);
        proc_.loadedSamplePaths.set(i, prepared.samplePaths[(size_t) i]);
        // Prepare MIDI engines for freshly-grown slots; existing slots keep theirs.
        if (i >= oldN && proc_.currentSampleRate > 0 && proc_.currentBlockSize > 0)
            proc_.midiEngines[(size_t) i].prepare(proc_.currentSampleRate, proc_.currentBlockSize);
        proc_.sequencer.updatePattern(i);
        proc_.sequencer.resetStepTrackingForSwap(i);
    }

    // Tear down slots that are no longer active (shrink case) — mirrors resizeRhythmArrays.
    for (int i = n; i < oldN; ++i)
    {
        proc_.voiceEngines[(size_t) i].reset();
        proc_.midiEngines[(size_t) i] = MidiOutputEngine{};
        proc_.mixerEngine.channels[(size_t) i].reset();
        proc_.loadedSamplePaths.set(i, juce::String());
    }

    if (n > oldN)
        proc_.numActiveRhythms.store(n, std::memory_order_release);  // grow: publish after slots ready
    }   // release rhythmsLock before resuming
    proc_.suspendProcessing(false);

    // ── APVTS / mixer / global finalize (message-thread, no I/O) ───────────────
    // The Rhythm + VoiceEngine are already live. Push the moved-in Rhythm into
    // APVTS so UI knobs + host automation reflect the new preset (Rhythm → APVTS,
    // matching the per-rhythm hot-swap finalize); apvtsLoading=true makes the
    // parameterChanged listener skip the engine re-sync so it can't clobber the
    // freshly-installed voice. Channel + global params come from the parsed tree.
    mu_core::ScopedApvtsLoading guard(proc_.apvtsLoading);
    const juce::ValueTree& root = prepared.tree;
    int rhythmIdx = 0;
    for (int ci = 0; ci < root.getNumChildren() && rhythmIdx < n; ++ci)
    {
        auto rTree = root.getChild(ci);
        if (rTree.getType() != juce::Identifier("Rhythm")) continue;
        const int i = rhythmIdx++;
        proc_.pushRhythmToAPVTS(i);
        restoreRhythmChannelParams(i, rTree);
    }
    restoreGlobalState(root);
}
