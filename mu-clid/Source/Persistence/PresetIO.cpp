#include "PresetIO.h"
#include "Plugin/PluginProcessor.h"
#include "Plugin/PluginProcessor_Internal.h"
#include "Persistence/PresetHelpers.h"      // writeKindedProperty, readKindedPropertyAsActualV2, kGlobalParamDefs
#include "Persistence/PresetMigrations.h"   // v3 insert/master/mod-assignment migrations
#include "Persistence/ModulatorSerialise.h" // serialiseModulators, deserialiseModulators, clearModulators
#include "Persistence/PresetFiles.h"        // mu_pp::safePresetFileName + replaceFileAtomically
#include "Persistence/EmbeddedSample.h"     // embedded samples: FLAC or raw file bytes
#include "Persistence/LegacyReverbDecay.h"  // a pre-Decay-knob preset's reverb Decay
#include "UI/Components/MuLookAndFeel.h" // kLayerPaletteSize
#include <limits>               // std::numeric_limits for NaN sentinel

namespace mu_clid {
using mu_pp::enumName;
using mu_pp::readEnumIndex;
using mu_clid::migrate::migrateInsertSlotsV3;       // moved to PresetMigrations
using mu_clid::migrate::migrateMasterInsertSlotsV3;
using mu_clid::migrate::migrateModAssignmentsV3;

// mu-Clid shares only the file-name rule with mu-core's PresetFiles helpers. Its on-disk
// format predates them and must keep loading: presetName / presetCategory properties on
// the root (not name / category), per-rhythm values written as actual kinded values
// (not normalised <p> rows), embedded sample data, atomic writes, and a categories.txt
// list that keeps user categories even when no preset uses them yet.

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
                                 const mu_clid::RhythmParamDef& def)
{
    const float actual = param.convertFrom0to1(param.getValue());
    writeKindedProperty(tree, propName, actual, def.kind, def.algorithmNames);
}

// readKindedPropertyAsActualV2 is now inline in PresetHelpers.h — see using declaration above.

// Global-state read wrapper (v2): NaN when the tree lacks the property.
static float readGlobalPropertyAsActual(const juce::ValueTree& tree,
                                         const juce::String& propName,
                                         const juce::RangedAudioParameter& /*param*/,
                                         const mu_clid::GlobalParamDef& def)
{
    if (! tree.hasProperty(propName))
        return std::numeric_limits<float>::quiet_NaN();
    return readKindedPropertyAsActualV2(tree, propName, def.kind, def.algorithmNames);
}

// Preset version gating removed — pre-distribution era, every preset just needs to work
// with the current build. Migration helpers (migrateInsertSlotsV3 etc.) still run
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

// Where a preset rhythm's sample comes from. An embedded sample (base64 `data` + its `name`) is
// written to a temp file; otherwise the stored path is resolved against the Samples folder,
// falling back to the file name there when an absolute path has moved.
struct PresetSample
{
    enum class Kind { None, BadEmbed, Embedded, Found, Moved, Missing };
    Kind         kind = Kind::None;
    juce::File   file;           // what to load (Embedded / Found / Moved)
    juce::String rememberPath;   // the sample path the slot records
    juce::String fileName;       // the stored sample's file name, for messages
    bool         relative = false;   // Missing: the stored path was relative (not in the Samples folder)
};

// Embed sample file `f` in a preset node: sampleData (base64), sampleName, and for a FLAC-encoded
// sample sampleDataCodec + sampleDataBits (see Persistence/EmbeddedSample.h).
static void embedSampleInto(juce::ValueTree& node, const juce::File& f)
{
    const auto e = mu_clid::embedded_sample::encode(f);
    if (e.data.getSize() == 0) return;
    node.setProperty("sampleData", juce::Base64::toBase64(e.data.getData(), e.data.getSize()), nullptr);
    node.setProperty("sampleName", f.getFileName(), nullptr);
    if (e.codec.isNotEmpty())
    {
        node.setProperty("sampleDataCodec", e.codec, nullptr);
        node.setProperty("sampleDataBits", e.bits, nullptr);
    }
}

static PresetSample resolvePresetSample(const juce::String& data, const juce::String& name,
                                        const juce::String& storedPath, const juce::File& samplesDir,
                                        const juce::String& codec = {}, int bits = 0)
{
    PresetSample r;
    if (data.isNotEmpty() && name.isNotEmpty())
    {
        juce::MemoryBlock mb;
        { juce::MemoryOutputStream mos(mb, false); juce::Base64::convertFromBase64(mos, data); }
        auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("muClid_samples");
        tempDir.createDirectory();
        const auto tempFile = tempDir.getChildFile(name);
        if (! mu_clid::embedded_sample::decodeTo(mb, codec, bits, tempFile))
        {
            r.kind = PresetSample::Kind::BadEmbed;
            return r;
        }
        r.kind = PresetSample::Kind::Embedded;
        r.file = tempFile;
        r.rememberPath = tempFile.getFullPathName();
        return r;
    }
    if (storedPath.isEmpty()) return r;   // Kind::None — the rhythm has no sample

    const juce::File f = resolveSamplePath(storedPath, samplesDir);
    r.fileName = f.getFileName();
    if (f.existsAsFile())
    {
        r.kind = PresetSample::Kind::Found;
        r.file = f;
        r.rememberPath = f.getFullPathName();
    }
    else if (juce::File::isAbsolutePath(storedPath))
    {
        const juce::File fallback = samplesDir.getChildFile(f.getFileName());
        if (fallback.existsAsFile())
        {
            r.kind = PresetSample::Kind::Moved;
            r.file = fallback;
            r.rememberPath = fallback.getFullPathName();
        }
        else
        {
            r.kind = PresetSample::Kind::Missing;
            r.rememberPath = storedPath;
        }
    }
    else
    {
        r.kind = PresetSample::Kind::Missing;
        r.relative = true;
        r.rememberPath = f.getFullPathName();
    }
    return r;
}

// Load a resolved sample into `voice` (clearing it when there is none) and tell the user about
// a moved — and, when `reportMissing`, a missing — sample; `context` is appended to the moved
// note (e.g. " (rhythm 3)"). Returns the path the slot records.
static juce::String applyPresetSample(const PresetSample& smp, VoiceEngine& voice,
                                      const std::function<void(const juce::String&)>& onError,
                                      bool reportMissing, const juce::String& context = {})
{
    using Kind = PresetSample::Kind;
    if (smp.kind == Kind::Embedded || smp.kind == Kind::Found || smp.kind == Kind::Moved)
        voice.loadFile(smp.file);
    else if (smp.kind != Kind::BadEmbed)
        voice.clearSample();

    if (onError)
    {
        if (smp.kind == Kind::Moved)
            onError("Sample '" + smp.fileName + "' not at original path, loaded from content folder instead"
                    + context + ".");
        else if (smp.kind == Kind::Missing && reportMissing)
            onError("Sample '" + smp.fileName
                    + juce::String::fromUTF8(smp.relative ? "' missing from content folder \xe2\x80\x94 rhythm loaded without audio."
                                                          : "' missing \xe2\x80\x94 rhythm loaded without audio."));
    }
    return smp.rememberPath;
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



// A rhythm preset file's state, or an invalid tree after reporting why (missing, unparseable,
// not a preset, or a legacy v0 / v1 preset — only v2 presets load).
juce::ValueTree PresetIO::readRhythmPresetFile(const juce::File& file) const
{
    auto fail = [this, &file](const juce::String& why)
    {
        if (proc.onLoadError) proc.onLoadError(why + file.getFileName());
        return juce::ValueTree();
    };
    if (! file.existsAsFile()) return fail("File missing: ");
    auto xml = juce::parseXML(file);
    if (! xml) return fail("Could not parse: ");
    auto state = juce::ValueTree::fromXml(*xml);
    if (! state.isValid()) return fail("Invalid preset: ");
    if (! requireSupportedPresetVersion(state, file.getFileName(), proc.onLoadError))
        return {};
    return state;
}

// The preset's identity onto `r`: its name — `presetName` (matches the dropdown entry and the
// file name) over the older `r0_name`, the slot's short historical name — and its colour.
void PresetIO::applyPresetIdentity(const juce::ValueTree& state, Rhythm& r)
{
    auto presetNameVal = state.getProperty("presetName");
    auto rhythmNameVal = state.getProperty("r0_name");
    if (presetNameVal.isString() && presetNameVal.toString().isNotEmpty())
        r.name = presetNameVal.toString().toStdString();
    else if (rhythmNameVal.isString() && rhythmNameVal.toString().isNotEmpty())
        r.name = rhythmNameVal.toString().toStdString();
    r.colourIndex = (int) state.getProperty("r0_colour", r.colourIndex);
}

//==============================================================================
// A .muRhythm load: the file is read here, then it goes through ProcessorBase's one layer-preset
// stage entry (which leaves no clip active) into stageRhythmPresetTree below.
void PresetIO::stageRhythmPreset(int rhythmIndex, const juce::File& file, bool keepIdentity)
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.sequencer.getNumRhythms()) return;

    const auto state = readRhythmPresetFile(file);
    if (! state.isValid()) return;

    loadSource = file.getFileName();   // names the file in load messages; the tree stage has no file
    proc.stageLayerPreset(rhythmIndex, state, keepIdentity, false);
    loadSource = juce::String();

    // A named preset load: a clip stored from this rhythm is named after the file. A settings reset is not one.
    if (! keepIdentity)
        proc.noteLayerPresetLabel(rhythmIndex, file.getFileNameWithoutExtension());
}

// Everything a layer-preset load does after the parse (file or clip). Stopped: install at once.
// Playing: the same prepared rhythm waits for its loop boundary.
void PresetIO::stageRhythmPresetTree(int rhythmIndex, const juce::ValueTree& state, bool keepIdentity)
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.sequencer.getNumRhythms() || ! state.isValid()) return;

    const juce::String source = loadSource.isNotEmpty() ? loadSource : juce::String("layer preset");
    if (! proc.sequencerPlaying.load())
    {
        applyRhythmPresetTree(state, rhythmIndex, keepIdentity, source);
        return;
    }
    auto prepared = prepareRhythmPresetTree(state, rhythmIndex, keepIdentity, source);
    if (! prepared.voice) return;
    proc.hotSwapStager.cancelPendingIfAny(rhythmIndex);   // a newer swap supersedes a pending one
    proc.hotSwapStager.stage(rhythmIndex, std::move(prepared.rhythm), std::move(prepared.voice), prepared.samplePath);

    // The transport may have stopped while the rhythm was being prepared. A stopped transport raises no boundary, so
    // nothing would ever commit the stage: re-read after a fence (as the full-preset path does) and, if stopped, drop the
    // stage and apply the tree here instead.
    std::atomic_thread_fence(std::memory_order_seq_cst);
    if (! proc.sequencerPlaying.load())
    {
        proc.hotSwapStager.cancelPendingIfAny(rhythmIndex);
        applyRhythmPresetTree(state, rhythmIndex, keepIdentity, source);
    }
}

// A layer-preset tree prepared for slot `rhythmIndex` (no voice when the tree can't be built).
// keepIdentity: the slot keeps its own name + colour (a settings reset or a clip launch, not a named preset).
PresetIO::PreparedRhythm PresetIO::prepareRhythmPresetTree(const juce::ValueTree& state, int rhythmIndex,
                                                           bool keepIdentity, const juce::String& source)
{
    if (! state.isValid()) return {};
    auto prepared = prepareRhythm(state, "r0_", proc.currentSampleRate, proc.currentBlockSize,
                                  proc.getSamplesDir(), proc.onLoadError, source);
    if (keepIdentity)
    {
        const Rhythm& current = proc.sequencer.getRhythm(rhythmIndex);
        prepared.rhythm.name        = current.name;
        prepared.rhythm.colourIndex = current.colourIndex;
    }
    return prepared;
}

//==============================================================================
// PluginProcessor::saveRhythmPreset deleted — was dead code. The only call
// site (RhythmContent::saveRhythmPreset) routes through saveRhythmPresetToFile
// instead, and the dead function had already drifted from its sibling (missing
// the modulator-child write). Removing it also fixes the ch_* mixer-
// channel write that this function emitted was never read back by any load path
// (mixer settings stay attached to the slot, not the rhythm preset), so deleting
// the function removes the dead write at the same time.

juce::StringArray PresetIO::loadCategoryList() const
{
    juce::StringArray cats;
    proc.getPresetsDir().getChildFile("categories.txt").readLines(cats);
    // Also scan .muClid and .muRhythm files for categories not yet in the list.
    auto scan = [&](const juce::File& dir, const juce::String& ext) {
        for (const auto& f : dir.findChildFiles(juce::File::findFiles, false, "*." + ext))
        {
            const auto cat = mu_pp::readPresetMeta(f).category;
            if (cat.isNotEmpty() && cat != "All" && !cats.contains(cat, false))
                cats.add(cat);
        }
    };
    scan(proc.getPresetsDir(), "muClid");
    scan(proc.getRhythmsDir(), "muRhythm");
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
        mu_pp::replaceFileAtomically(proc.getPresetsDir().getChildFile("categories.txt"),
                              cats.joinIntoString("\n"), proc.onLoadError);
    }
}

// The rhythm's layer-preset tree: its sequencer-page parameters, voice chain, envelopes, insert
// effect, modulators and sample reference. A file (.muRhythm) also carries its preset metadata and
// the rhythm's name + colour; a clip (forFile false) carries neither, and always the sample path
// only, never the audio.
juce::ValueTree PresetIO::buildRhythmPresetTree(int rhythmIndex, bool embedSample, bool forFile,
                                                const juce::String& presetName, const juce::String& category,
                                                const juce::String& description)
{
    juce::ValueTree state("MuClidRhythm");
    if (forFile)
    {
        state.setProperty("presetName",         presetName,                nullptr);
        state.setProperty("presetCategory",     category,                  nullptr);
        state.setProperty("presetDescription",  description,               nullptr);
        state.setProperty("presetEmbedSamples", embedSample ? 1 : 0,       nullptr);
        // presetVersion property dropped: not distributing yet, current-build-only.
    }

    const Rhythm& r = proc.sequencer.getRhythm(rhythmIndex);
    if (forFile)
    {
        state.setProperty("r0_name",   juce::String(r.name), nullptr);
        state.setProperty("r0_colour", r.colourIndex,        nullptr);
    }
    state.setProperty("r0_sample",
                      embedSample
                          ? juce::String()
                          : toRelativeSamplePath(proc.samples.path(rhythmIndex), proc.getSamplesDir()),
                      nullptr);

    // Rhythm presets store ONLY proc.sequencer-page state (Euclidean params, voice chain,
    // envelopes, insert effect). Mixer-page state (channel level/pan/sends/sidechain/
    // output bus) is not part of a layer preset (.muRhythm); the strip is not carried by it.
    // Stage 35: v2 writes actual values + algorithm-name strings via
    // writeParamPropertyV2. Ints / bools / algorithm selectors get their
    // natural representation in XML; floats get raw actual values.
    const juce::String srcPrefix = "r" + juce::String(rhythmIndex) + "_";
    for (int i = 0; i < kRhythmParamCount; ++i)
        if (auto* param = proc.apvts.getParameter(srcPrefix + kRhythmParamDefs[i].suffix))
            writeParamPropertyV2(state,
                                 "r0_" + juce::String(kRhythmParamDefs[i].suffix),
                                 *param,
                                 kRhythmParamDefs[i]);

    // serialise modulators (ControlSequences + ModulationMatrix assignments).
    state.addChild(serialiseModulators(r), -1, nullptr);

    if (embedSample)
    {
        const juce::String path = proc.samples.path(rhythmIndex);
        if (path.isNotEmpty())
        {
            juce::File f(path);
            if (f.existsAsFile())
            {
                embedSampleInto(state, f);
            }
        }
    }
    return state;
}

// What a clip stores: the rhythm as a layer-preset file would hold it, minus the file metadata and
// identity. An invalid tree when the slot is out of range.
juce::ValueTree PresetIO::captureRhythmPresetTree(int rhythmIndex)
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.sequencer.getNumRhythms()) return {};
    return buildRhythmPresetTree(rhythmIndex, false, false, {}, {}, {});
}

void PresetIO::saveRhythmPresetToFile(int rhythmIndex, const juce::File& destFile,
                                             bool embedSample, const juce::String& category,
                                             const juce::String& description)
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.sequencer.getNumRhythms()) return;

    // if the current sample comes from an embedded-sample decode (path
    // points into %TEMP%/muClid_samples/), force-embed so we never write the
    // ephemeral temp path as `r0_sample`. The temp file would not survive an
    // OS reboot, so any later load would lose the sample.
    if (isEmbeddedSampleTempPath(proc.samples.path(rhythmIndex)) && ! embedSample)
    {
        embedSample = true;
        if (proc.onLoadError)
            proc.onLoadError("Sample originated from embedded data; saving with embed forced on.");
    }

    const auto state = buildRhythmPresetTree(rhythmIndex, embedSample, true,
                                             destFile.getFileNameWithoutExtension(), category, description);
    mu_pp::replaceFileAtomically(destFile, state.toXmlString(), proc.onLoadError);

    // The rhythm is now that saved preset (the "_default" template is not a preset the user named).
    if (! destFile.getFileName().startsWith("_"))
        proc.noteLayerPresetLabel(rhythmIndex, destFile.getFileNameWithoutExtension());
}

// Load a .muRhythm into a slot at once, whatever the transport is doing. Only sequencer-page state
// loads: a layer preset (.muRhythm) does not carry the mixer strip, so the slot's strip is left as it is.
bool PresetIO::applyRhythmPreset(const juce::File& file, int targetIndex, bool keepIdentity)
{
    if (targetIndex < 0 || targetIndex >= proc.sequencer.getNumRhythms()) return false;
    const auto state = readRhythmPresetFile(file);
    if (! state.isValid()) return false;
    if (! applyRhythmPresetTree(state, targetIndex, keepIdentity, file.getFileName())) return false;

    // A direct load is not a clip launch: the install keeps the slot's activeClip, so clear it here.
    proc.sequencer.getRhythm(targetIndex).activeClip = -1;
    proc.getLiveState().clearLayerClips(targetIndex);
    proc.noteLayerPresetLabel(targetIndex, keepIdentity ? juce::String() : file.getFileNameWithoutExtension());
    return true;
}

// The same prepared rhythm and the same install as the loop-boundary swap, then the rhythm pushed to
// APVTS so the knobs follow.
bool PresetIO::applyRhythmPresetTree(const juce::ValueTree& state, int targetIndex, bool keepIdentity,
                                     const juce::String& source)
{
    if (targetIndex < 0 || targetIndex >= proc.sequencer.getNumRhythms()) return false;
    auto prepared = prepareRhythmPresetTree(state, targetIndex, keepIdentity, source);
    if (! prepared.voice) return false;

    proc.hotSwapStager.cancelPendingIfAny(targetIndex);
    // Declared before the lock so a force-cut engine destructs after suspend and the lock are released.
    std::unique_ptr<VoiceEngine> cutVictim;
    proc.suspendProcessing(true);
    {
        const juce::ScopedLock sl(proc.layersLock);
        proc.hotSwapStager.installRhythm(targetIndex, std::move(prepared.rhythm), std::move(prepared.voice),
                                          prepared.samplePath, 0, RhythmSequencer::kAnyFrameEpoch, &cutVictim);
    }
    proc.suspendProcessing(false);

    {
        mu_core::ScopedApvtsLoading guard(proc.apvtsLoading);
        proc.pushRhythmToApvts(targetIndex);
    }

    // The editor ignores the APVTS pushes above (they are a bulk load), so it re-binds on this callback, as it
    // does for a boundary commit; without it a stopped clip launch changes the sound but not the screen.
    if (proc.onLayerPresetCommitted)
        proc.onLayerPresetCommitted(targetIndex);
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
    const juce::File f = proc.getRhythmsDir().getChildFile("_default.muRhythm");
    if (!f.existsAsFile()) return false;

    // "Default rhythm" is a sequencer / voice settings reset — NOT an identity
    // change: the slot keeps its name and colour (assigned when it was added), on the
    // stopped (immediate) and playing (staged) paths alike.
    stageRhythmPreset(rhythmIndex, f, /*keepIdentity*/ true);
    return true;
}

void PresetIO::loadDefaultPreset()
{
    juce::File f = proc.getDefaultPresetFile();
    if (f.existsAsFile())
    {
        loadPreset(f);
        return;
    }
    // Fall back to a single-rhythm `_default.muRhythm` if the full-session default
    // isn't present. Used by the listening-test pipeline to set a known starting
    // state on standalone launch without needing a complete `.muClid` file.
    const juce::File rhy = proc.getRhythmsDir().getChildFile("_default.muRhythm");
    if (rhy.existsAsFile() && proc.getNumRhythms() > 0)
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
    writeFullPresetFile(proc.getPresetsDir().getChildFile(mu_pp::safePresetFileName(name, "Preset") + ".muClid"),
                        name, description, category, embedSamples);
}

bool PresetIO::saveFullPresetTo(const juce::File& file)
{
    return writeFullPresetFile(file, file.getFileNameWithoutExtension(), {}, {}, false);
}

// A full preset written to `file`. A sample that lives in the embedded-decode temp dir forces
// embedding: the temp file is ephemeral, so writing its path would break the preset on next load.
bool PresetIO::writeFullPresetFile(const juce::File& file, const juce::String& name, const juce::String& description,
                                   const juce::String& category, bool embedSamples)
{
    if (! embedSamples)
    {
        for (int i = 0; i < proc.sequencer.getNumRhythms(); ++i)
        {
            if (isEmbeddedSampleTempPath(proc.samples.path(i)))
            {
                embedSamples = true;
                if (proc.onLoadError)
                    proc.onLoadError("One or more samples originated from embedded data; saving with embed forced on.");
                break;
            }
        }
    }
    return mu_pp::replaceFileAtomically(file, buildFullPresetTree(name, description, category, embedSamples, false)
                                                  .toXmlString(),
                                        proc.onLoadError);
}

// The rhythm's clip bank and playing clip onto its <Rhythm> node. Clips are sample-path-only, so a
// preset (not a session) blanks a path into the ephemeral embedded-decode folder, as it does for the
// rhythm's own sample. An empty bank writes nothing.
static void writeRhythmClips(juce::ValueTree& rTree, const Rhythm& r, bool forSession)
{
    mu_core::ClipBank bank = *r.clips;
    if (! forSession)
        for (int c = 0; c < mu_core::ClipBank::kCount; ++c)
        {
            auto clip = bank.get(c);
            if (clip.isValid() && isEmbeddedSampleTempPath(clip.getProperty("r0_sample").toString()))
            {
                clip.setProperty("r0_sample", juce::String(), nullptr);
                bank.store(c, clip, bank.getName(c));
            }
        }
    bank.writeTo(rTree);
    if (r.activeClip >= 0 && bank.isOccupied(r.activeClip))
        rTree.setProperty("activeClip", r.activeClip, nullptr);
}

// The .muClid tree: every active rhythm (its sequencer-page params, name, colour, sample, mixer
// strip and modulators) plus the global FX / mixer state. A session (forSession) is the same
// tree, keeping temp-dir sample paths.
juce::ValueTree PresetIO::buildFullPresetTree(const juce::String& name, const juce::String& description,
                                              const juce::String& category, bool embedSamples, bool forSession)
{
    const int n = proc.sequencer.getNumRhythms();
    juce::ValueTree root("MuClidPreset");
    root.setProperty("presetName",         name,                 nullptr);
    root.setProperty("presetDescription",  description,          nullptr);
    root.setProperty("presetEmbedSamples", embedSamples ? 1 : 0, nullptr);
    root.setProperty("presetCategory",    category,    nullptr);
    // presetVersion property dropped: not distributing yet, current-build-only.

    for (int i = 0; i < n; ++i)
    {
        const Rhythm& r = proc.sequencer.getRhythm(i);
        juce::ValueTree rTree("Rhythm");
        rTree.setProperty("name",   juce::String(r.name), nullptr);
        rTree.setProperty("colour", r.colourIndex,         nullptr);
        // A preset drops a temp-dir path (the embedded sampleData below carries the bytes); a
        // session keeps it, as the decoded file outlives the project reload.
        rTree.setProperty("sample",
                          isEmbeddedSampleTempPath(proc.samples.path(i))
                              ? (forSession ? proc.samples.path(i) : juce::String())
                              : toRelativeSamplePath(proc.samples.path(i), proc.getSamplesDir()),
                          nullptr);

        // Stage 35: v2 writes per the param's ParamKind — actual values for
        // floats, ints, bools as "true"/"false", algorithm selectors as the
        // stable name string.
        const juce::String srcPrefix = "r" + juce::String(i) + "_";
        for (int j = 0; j < kRhythmParamCount; ++j)
            if (auto* param = proc.apvts.getParameter(srcPrefix + kRhythmParamDefs[j].suffix))
                writeParamPropertyV2(rTree,
                                     juce::String(kRhythmParamDefs[j].suffix),
                                     *param,
                                     kRhythmParamDefs[j]);

        const juce::String chSrcPrefix = "ch" + juce::String(i) + "_";
        for (int j = 0; kChannelSuffixes[j] != nullptr; ++j)
            if (auto* param = proc.apvts.getParameter(chSrcPrefix + kChannelSuffixes[j]))
                rTree.setProperty("ch_" + juce::String(kChannelSuffixes[j]), param->getValue(), nullptr);

        // serialise modulators per rhythm.
        rTree.addChild(serialiseModulators(r), -1, nullptr);
        writeRhythmClips(rTree, r, forSession);

        if (embedSamples)
        {
            const juce::String path = proc.samples.path(i);
            if (path.isNotEmpty())
            {
                juce::File f(path);
                if (f.existsAsFile())
                {
                    embedSampleInto(rTree, f);
                }
            }
        }

        root.addChild(rTree, -1, nullptr);
    }

    // Save global FX/mixer state so a preset fully restores the session.
    // Stage 35: v2 writes GlobalState per the ParamKind in kGlobalParamDefs.
    // eff_algo / rev_algo / mst_insChar / mst_ins2Char emit their stable algorithm
    // name string; bool params emit "true"/"false"; the rest emit actual values.
    juce::ValueTree globalTree("GlobalState");
    for (int i = 0; i < mu_clid::kGlobalParamDefCount; ++i)
    {
        const auto& def = mu_clid::kGlobalParamDefs[i];
        if (auto* param = proc.apvts.getParameter(def.id))
        {
            const float actual = param->convertFrom0to1(param->getValue());
            writeKindedProperty(globalTree, juce::String(def.id), actual, def.kind, def.algorithmNames);
        }
    }
    root.addChild(globalTree, -1, nullptr);

    return root;
}

// ── PresetIO::loadPreset helpers ────────────────────────────────────────────
//
// loadPreset itself is a sequence of named steps; each step lives in one of the
// helpers below. All run on the message thread under a single
// mu_core::ScopedApvtsLoading guard scoped to the caller.

void PresetIO::restoreRhythmChannelParams(int apvtsSlot, const juce::ValueTree& rTree)
{
    const juce::String dstChPrefix = "ch" + juce::String(apvtsSlot) + "_";
    for (int j = 0; kChannelSuffixes[j] != nullptr; ++j)
    {
        juce::Identifier chPropId { "ch_" + juce::String(kChannelSuffixes[j]) };
        if (rTree.hasProperty(chPropId))
            if (auto* param = proc.apvts.getParameter(dstChPrefix + kChannelSuffixes[j]))
                param->setValueNotifyingHost((float)rTree.getProperty(chPropId));
    }
}

void PresetIO::restoreRhythmSample(int i, const juce::ValueTree& tree,
                                    const juce::String& samplePathProp,
                                    const juce::String& sampleDataProp,
                                    const juce::String& sampleNameProp)
{
    // A slot without an engine has nothing to load a sample into. (Mirrors the null guard
    // in forceSyncRhythmFromApvts.)
    if (! proc.voiceEngines[i]) return;

    const auto smp = resolvePresetSample(tree.getProperty(juce::Identifier(sampleDataProp)).toString(),
                                         tree.getProperty(juce::Identifier(sampleNameProp)).toString(),
                                         tree.getProperty(juce::Identifier(samplePathProp)).toString(),
                                         proc.getSamplesDir(),
                                         tree.getProperty(juce::Identifier(sampleDataProp + "Codec")).toString(),
                                         (int) tree.getProperty(juce::Identifier(sampleDataProp + "Bits"), 0));
    if (smp.kind == PresetSample::Kind::BadEmbed) return;   // unreadable embedded data: leave the slot as it is
    proc.samples.setPath(i, applyPresetSample(smp, *proc.voiceEngines[i], proc.onLoadError, false,
                                                     " (rhythm " + juce::String(i + 1) + ")"));
}

void PresetIO::restoreGlobalState(const juce::ValueTree& root)
{
    // GlobalState child was added later; older files omit it, which leaves every global where it
    // was. Stage 35: iterate kGlobalParamDefs so algorithm-selector params (eff_algo / rev_algo /
    // mst_insChar / mst_ins2Char) use the name-string lookup path in v2.
    // Stage 36 v3 migration: collapse master-insert named fields → insP1..4 (algo-aware).
    // Mutates a copy so the original child is untouched.
    juce::ValueTree migrated = root.getChildWithName("GlobalState").createCopy();
    if (! migrated.isValid()) migrated = juce::ValueTree("GlobalState");
    migrateMasterInsertSlotsV3(migrated, 1);
    migrateMasterInsertSlotsV3(migrated, 2);

    // A file saved before the reverb Decay knob: the Decay its algorithm had then (the saved
    // algorithm, or the current one when the file leaves rev_algo alone), never the last preset's.
    if (! migrated.hasProperty(mu_pp::kReverbDecayId))
        if (auto* revAlgo = proc.apvts.getParameter(mu_pp::kReverbAlgoId))
        {
            int algorithm = mu_pp::currentReverbAlgorithm(*revAlgo);
            for (int gi = 0; gi < mu_clid::kGlobalParamDefCount; ++gi)
                if (juce::String(mu_clid::kGlobalParamDefs[gi].id) == mu_pp::kReverbAlgoId)
                    if (const float saved = readGlobalPropertyAsActual(migrated, mu_pp::kReverbAlgoId, *revAlgo,
                                                                       mu_clid::kGlobalParamDefs[gi]);
                        ! std::isnan(saved))
                        algorithm = juce::roundToInt(saved);
            migrated.setProperty(mu_pp::kReverbDecayId, mu_pp::legacyReverbDecay(algorithm), nullptr);
        }

    for (int gi = 0; gi < mu_clid::kGlobalParamDefCount; ++gi)
    {
        const auto& def = mu_clid::kGlobalParamDefs[gi];
        if (auto* param = proc.apvts.getParameter(def.id))
        {
            const float actualVal = readGlobalPropertyAsActual(migrated, juce::String(def.id),
                                                               *param, def);
            if (! std::isnan(actualVal))
                param->setValueNotifyingHost(param->convertTo0to1(actualVal));
        }
    }
}

// ── Rhythm preparation ──────────────────────────────────────────────────────
//
// One rhythm built off the audio thread from a preset node — a .muRhythm root (properties
// prefixed "r0_") or a .muClid <Rhythm> child (unprefixed). It starts from a default Rhythm, so
// whatever the node doesn't carry is the default (as in every product); then its modulators and
// a primed VoiceEngine with the node's sample loaded. The expensive bits — sample decode / disk
// load and VoiceEngine::prepareToPlay — happen here, so every install (stopped load, loop-boundary
// swap, full preset, session) is just in-memory moves.
PresetIO::PreparedRhythm PresetIO::prepareRhythm(const juce::ValueTree& nodeIn, const juce::String& prefix,
                                                 double sampleRate, int blockSize, const juce::File& samplesDir,
                                                 const std::function<void(const juce::String&)>& onLoadError,
                                                 const juce::String& source)
{
    PreparedRhythm out;
    Rhythm& r = out.rhythm;

    // v3 insert-slot migration (algo-aware) on a copy, before the params are read.
    juce::ValueTree node = nodeIn.createCopy();
    migrateInsertSlotsV3(node, prefix);

    // Sequencer-page params: each one the node carries, onto the default.
    for (int j = 0; j < kRhythmParamCount; ++j)
    {
        const auto& def = kRhythmParamDefs[j];
        const juce::String propName = prefix + def.suffix;
        if (! node.hasProperty(propName)) continue;
        const float actualVal = readKindedPropertyAsActualV2(node, propName, def.kind, def.algorithmNames);
        if (std::isnan(actualVal)) continue;
        bool pd = false, vd = false;
        applyRhythmSuffix(def.suffix, actualVal, r, pd, vd);
    }

    // Name + colour: a .muClid child's own; a .muRhythm's presetName (matches the dropdown entry
    // and the file name) over its older, shorter r0_name.
    if (prefix.isEmpty())
    {
        const auto nameVal = node.getProperty("name");
        if (nameVal.isString() && nameVal.toString().isNotEmpty())
            r.name = nameVal.toString().toStdString();
        r.colourIndex = (int) node.getProperty("colour", r.colourIndex);
    }
    else
    {
        applyPresetIdentity(node, r);
    }

    // Modulators, with old insert.* / ks.* / voc.* destination ids translated first.
    if (auto mods = node.getChildWithName("Modulators"); mods.isValid())
    {
        const int algoIndex = mu_audio::indexFromName(mu_audio::kInsertAlgorithmNames,
                                                    node.getProperty(prefix + "drvChar").toString());
        migrateModAssignmentsV3(mods, algoIndex);
        clearModulators(r);
        auto dropped = deserialiseModulators(mods, r);
        if (! dropped.isEmpty() && onLoadError)
            onLoadError("Dropped " + juce::String(dropped.size()) + " modulator assignment(s) from "
                        + source + ": " + dropped.joinIntoString("; "));
    }

    // The voice engine, built and primed with the rhythm's VoiceParams off the audio thread.
    out.voice = std::make_unique<VoiceEngine>();
    out.voice->prepareToPlay(sampleRate, blockSize);
    out.voice->setParams(r.voiceParams);

    // Sample: embedded blob first, else the stored path.
    const auto smp = resolvePresetSample(node.getProperty("sampleData").toString(),
                                         node.getProperty("sampleName", "embedded").toString(),
                                         node.getProperty(prefix + "sample").toString(), samplesDir,
                                         node.getProperty("sampleDataCodec").toString(),
                                         (int) node.getProperty("sampleDataBits", 0));
    out.samplePath = applyPresetSample(smp, *out.voice, onLoadError, true);
    return out;
}

// Build the whole prepared payload from a parsed MuClidPreset root (at most `maxRhythms` rhythms:
// an unlicensed build activates no more than its demo cap).
static HotSwapStager::PreparedFullPreset
buildPreparedFullPreset(const juce::ValueTree& root, double sampleRate, int blockSize,
                        const juce::File& samplesDir,
                        const std::function<void(const juce::String&)>& onLoadError, int maxRhythms)
{
    HotSwapStager::PreparedFullPreset out;
    out.tree = root;

    int n = 0;
    for (int ci = 0; ci < root.getNumChildren(); ++ci)
        if (root.getChild(ci).getType() == juce::Identifier("Rhythm")) ++n;
    n = juce::jlimit(1, juce::jmin(maxRhythms, (int) RhythmSequencer::MaxRhythms), n);
    out.numRhythms = n;

    // Prepare each <Rhythm> child into its slot.
    int idx = 0;
    for (int ci = 0; ci < root.getNumChildren() && idx < n; ++ci)
    {
        auto rTree = root.getChild(ci);
        if (rTree.getType() != juce::Identifier("Rhythm")) continue;
        const int i = idx++;
        auto prepared = PresetIO::prepareRhythm(rTree, {}, sampleRate, blockSize, samplesDir, onLoadError,
                                                "rhythm " + juce::String(i + 1));
        out.rhythms[(size_t) i]     = std::move(prepared.rhythm);
        out.voices[(size_t) i]      = std::move(prepared.voice);
        out.samplePaths[(size_t) i] = prepared.samplePath;

        // The slot's clip bank, read once here; the commit hands it to the live layer. Only a rhythm-shaped
        // payload is kept, and a playing clip must name a stored one.
        auto& bank = out.clipBanks[(size_t) i];
        bank.readFrom(rTree);
        for (int c = 0; c < mu_core::ClipBank::kCount; ++c)
            if (bank.isOccupied(c) && bank.get(c).getType() != juce::Identifier("MuClidRhythm"))
                bank.clear(c);
        const int active = (int) rTree.getProperty("activeClip", -1);
        out.activeClips[(size_t) i] = bank.isOccupied(active) ? active : -1;
    }
    return out;
}

HotSwapStager::PreparedFullPreset PresetIO::prepareFullPreset(const juce::ValueTree& root) const
{
    return buildPreparedFullPreset(root, proc.currentSampleRate, proc.currentBlockSize, proc.getSamplesDir(),
                                   proc.onLoadError,
                                   (int) RhythmSequencer::MaxRhythms);
}

bool PresetIO::loadPreset(const juce::File& file)
{
    auto xml = juce::parseXML(file);
    if (! xml)
    {
        proc.noteFullPresetLoadFailed();
        if (proc.onLoadError) proc.onLoadError("Could not parse: " + file.getFileName());
        return false;
    }
    auto root = juce::ValueTree::fromXml(*xml);
    if (! root.isValid())
    {
        proc.noteFullPresetLoadFailed();
        if (proc.onLoadError) proc.onLoadError("Invalid preset: " + file.getFileName());
        return false;
    }

    // A non-MuClidPreset root is host / project state (the getStateInformation
    // format), not a .muclid preset — restore it directly.
    if (root.getType() != juce::Identifier("MuClidPreset"))
    {
        restoreStateFromTree(root);
        return true;
    }

    // Full .muclid preset — ONE unified path. Pre-build every Rhythm + VoiceEngine +
    // sample off the audio thread (buildPreparedFullPreset), then just trigger the
    // switch: when playing, defer the flip to the next loop point (stageFullPreset →
    // boundary commit); when stopped, flip immediately (commitStagedFullPreset). Same
    // build + same commit code — only the trigger differs, so stopped and playing loads
    // are identical by construction (no divergent second path) and the stopped
    // load is glitch-free with its sample disk I/O done off the layersLock.
    auto prepared = prepareFullPreset(root);
    if (proc.sequencerPlaying.load())
    {
        proc.hotSwapStager.stageFullPreset(std::move(prepared));

        // The transport may have stopped between the read and the staging; the stop edge could already
        // have passed, so nothing would ever flag the payload. Re-read after a fence (as LayerSwapper::useFull
        // does) and, if stopped, take the payload back and apply it here.
        std::atomic_thread_fence(std::memory_order_seq_cst);
        if (! proc.sequencerPlaying.load())
        {
            HotSwapStager::PreparedFullPreset takenBack;
            if (proc.hotSwapStager.takeStagedFullPreset(takenBack))
                commitStagedFullPreset(takenBack);
        }
    }
    else
    {
        // Applied at once: an earlier full preset or per-rhythm swap still staged from while the transport
        // played must not land on top of this one at the stop edge.
        proc.hotSwapStager.cancelAllStaged();
        commitStagedFullPreset(prepared);
    }
    return true;
}

//==============================================================================
void PresetIO::commitStagedFullPreset(HotSwapStager::PreparedFullPreset& prepared, int originEff, uint32_t frameEpoch)
{
    const int n    = prepared.numRhythms;
    const int oldN = proc.numLayers.load(std::memory_order_acquire);

    // ── Install the pre-built voices + rhythms under suspend + layersLock ─────
    // suspendProcessing stops FUTURE processBlock calls; layersLock serialises with
    // any IN-FLIGHT one (suspend alone doesn't block it). Everything here is
    // in-memory (no parse, no disk I/O — done at stage time), so the lock is held for
    // microseconds and the swap stays glitch-free. The lock is released before the
    // APVTS finalize below so that (post-resume) work doesn't bail the audio thread.
    // Engines that must not destruct under the lock (force-cut victims, shrunk slots) land here and
    // free after it is released.
    std::array<std::unique_ptr<VoiceEngine>, RhythmSequencer::MaxRhythms> cutVictims, shrunkEngines;
    proc.suspendProcessing(true);
    {
        const juce::ScopedLock lw(proc.layoutWriteLock);   // first: the count stores below advance the layout serial
        const juce::ScopedLock sl(proc.layersLock);

    proc.setRhythmCount(n);

    if (n < oldN)
        proc.storeLayerCount(n, std::memory_order_release);  // shrink: drop count first

    for (int i = 0; i < n; ++i)
    {
        // The shared install: the outgoing engine retires and keeps rendering its tail while the
        // new preset plays, instead of being hard-cut. Grown slots have no old engine.
        proc.hotSwapStager.installRhythm(i, std::move(prepared.rhythms[(size_t) i]),
                                          std::move(prepared.voices[(size_t) i]), prepared.samplePaths[(size_t) i],
                                          originEff, frameEpoch, &cutVictims[(size_t) i]);
        // Prepare MIDI engines for freshly-grown slots; existing slots keep theirs.
        if (i >= oldN && proc.currentSampleRate > 0 && proc.currentBlockSize > 0)
            proc.midiEngines[(size_t) i].prepare(proc.currentSampleRate, proc.currentBlockSize);
    }

    // Tear down slots that are no longer active (shrink case). The voice engine is parked for a free
    // after the lock; the MIDI engine and mixer channel are plain values / atomics that the audio thread
    // reads, so they reset here without freeing anything; the sample path is message-thread only and is
    // cleared after the lock, so its string free never sits under it.
    for (int i = n; i < oldN; ++i)
    {
        shrunkEngines[(size_t) i] = std::move(proc.voiceEngines[(size_t) i]);
        proc.parkPendingMidiOff(i);   // keep the owed note-off; the audio thread no longer visits this slot
        proc.midiEngines[(size_t) i] = MidiOutputEngine{};
        proc.mixerEngine.channels[(size_t) i].reset();
    }

    if (n > oldN)
        proc.storeLayerCount(n, std::memory_order_release);  // grow: publish after slots ready
    }   // release layersLock before resuming
    proc.suspendProcessing(false);

    for (int i = n; i < oldN; ++i)
        proc.samples.clearPath(i);

    // Clip banks follow the preset: a live slot takes the bank its node carried (assigning a Rhythm
    // leaves the slot's bank alone, so the install above did not), a slot that dropped out empties.
    // After the lock: freeing trees is not a microsecond job.
    for (int i = 0; i < n; ++i)
    {
        Rhythm& r = proc.sequencer.getRhythm(i);
        *r.clips      = std::move(prepared.clipBanks[(size_t) i]);
        r.activeClip  = prepared.activeClips[(size_t) i];
    }
    for (int i = n; i < oldN; ++i)
        proc.clearLayerClips(i);
    proc.publishClipMasks();
    proc.noteFullPresetApplied();   // every apply path (boundary, stopped, host restore) goes through here: the live state drops its clips on this count

    // ── APVTS / mixer / global finalize (message-thread, no I/O) ───────────────
    // The Rhythm + VoiceEngine are already live. Push the moved-in Rhythm into
    // APVTS so UI knobs + host automation reflect the new preset (Rhythm → APVTS,
    // matching the per-rhythm hot-swap finalize); apvtsLoading=true makes the
    // parameterChanged listener skip the engine re-sync so it can't clobber the
    // freshly-installed voice. Channel + global params come from the parsed tree.
    mu_core::ScopedApvtsLoading guard(proc.apvtsLoading);
    const juce::ValueTree& root = prepared.tree;
    int rhythmIndex = 0;
    for (int ci = 0; ci < root.getNumChildren() && rhythmIndex < n; ++ci)
    {
        auto rTree = root.getChild(ci);
        if (rTree.getType() != juce::Identifier("Rhythm")) continue;
        const int i = rhythmIndex++;
        proc.pushRhythmToApvts(i);
        restoreRhythmChannelParams(i, rTree);
    }
    restoreGlobalState(root);
}

} // namespace mu_clid
