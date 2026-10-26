#include "RhythmPresetTree.h"
#include "Audio/VoiceEngine.h"
#include "Audio/AlgorithmNames.h"              // mu_audio::kInsertAlgorithmNames
#include "Persistence/RhythmParamTable.h"      // kRhythmParamDefs, findRhythmParamDef
#include "Persistence/PresetHelpers.h"         // writeKindedProperty, readKindedPropertyAsActualV2, kGlobalParamDefs
#include "Persistence/PresetMigrations.h"      // v3 insert/master/mod-assignment migrations
#include "Persistence/ModulatorSerialise.h"    // serialiseModulators, deserialiseModulators, clearModulators
#include "Persistence/EmbeddedSample.h"        // embedded samples: FLAC or raw file bytes
#include "Persistence/LegacyReverbDecay.h"     // a pre-Decay-knob preset's reverb Decay
#include <limits>                              // std::numeric_limits for NaN sentinel

namespace mu_clid::rhythm_preset {
using mu_clid::migrate::migrateInsertSlotsV3;
using mu_clid::migrate::migrateMasterInsertSlotsV3;
using mu_clid::migrate::migrateModAssignmentsV3;

namespace {

// Writes one APVTS parameter's actual value onto `tree` per its ParamKind (v2: human-readable and
// range-stable: bools "true" / "false", ints, algorithm selectors by name, floats as actual values).
void writeParamPropertyV2(juce::ValueTree& tree,
                          const juce::String& propName,
                          const juce::RangedAudioParameter& param,
                          const mu_clid::RhythmParamDef& def)
{
    const float actual = param.convertFrom0to1(param.getValue());
    writeKindedProperty(tree, propName, actual, def.kind, def.algorithmNames);
}

// Global-state read wrapper (v2): NaN when the tree lacks the property.
float readGlobalPropertyAsActual(const juce::ValueTree& tree,
                                 const juce::String& propName,
                                 const juce::RangedAudioParameter& /*param*/,
                                 const mu_clid::GlobalParamDef& def)
{
    if (! tree.hasProperty(propName))
        return std::numeric_limits<float>::quiet_NaN();
    return readKindedPropertyAsActualV2(tree, propName, def.kind, def.algorithmNames);
}

// Resolve a stored sample path to a juce::File to load. Relative paths
// (no drive letter / leading separator) are resolved against samplesDir so
// shipped presets with "kick.wav" find the file without a warning. Absolute
// paths are returned as-is; the caller's fallback logic handles "not found".
juce::File resolveSamplePath(const juce::String& storedPath, const juce::File& samplesDir)
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
void embedSampleInto(juce::ValueTree& node, const juce::File& f)
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

PresetSample resolvePresetSample(const juce::String& data, const juce::String& name,
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
juce::String applyPresetSample(const PresetSample& smp, VoiceEngine& voice,
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

// The preset's identity onto `r`: its name — `presetName` (matches the dropdown entry and the
// file name) over the older `r0_name`, the slot's short historical name — and its colour.
void applyPresetIdentity(const juce::ValueTree& state, Rhythm& r)
{
    auto presetNameVal = state.getProperty("presetName");
    auto rhythmNameVal = state.getProperty("r0_name");
    if (presetNameVal.isString() && presetNameVal.toString().isNotEmpty())
        r.name = presetNameVal.toString().toStdString();
    else if (rhythmNameVal.isString() && rhythmNameVal.toString().isNotEmpty())
        r.name = rhythmNameVal.toString().toStdString();
    r.colourIndex = (int) state.getProperty("r0_colour", r.colourIndex);
}

// The rhythm's clip bank and playing clip onto its <Rhythm> node. Clips are sample-path-only, so a
// preset (not a session) blanks a path into the ephemeral embedded-decode folder, as it does for the
// rhythm's own sample. An empty bank writes nothing.
void writeRhythmClips(juce::ValueTree& rTree, const Rhythm& r, bool forSession)
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

// The sample file at `path` embedded in `node`, when there is one.
void embedSampleAt(juce::ValueTree& node, const juce::String& path)
{
    if (path.isNotEmpty())
    {
        juce::File f(path);
        if (f.existsAsFile())
            embedSampleInto(node, f);
    }
}

} // namespace

// When the sample path is inside the Samples content folder, return a path
// relative to that folder (e.g. "kicks/kick.wav") so presets are
// machine-agnostic. Otherwise return absPath unchanged.
juce::String toRelativeSamplePath(const juce::String& absPath, const juce::File& samplesDir)
{
    if (absPath.isEmpty()) return absPath;
    juce::File f(absPath);
    if (f.isAChildOf(samplesDir))
        return f.getRelativePathFrom(samplesDir);
    return absPath;
}

// detect a sample path that points into our embedded-sample decode temp
// dir (%TEMP%/muClid_samples/). These paths come from loading a preset whose
// `sampleData` was base64-decoded into a temp file by the load path; they are
// NOT durable references — the temp dir gets wiped between OS sessions, and
// any subsequent .muRhythm / .muClid save that records the temp path as
// `r0_sample` would silently break on next load.
bool isEmbeddedSampleTempPath(const juce::String& path)
{
    if (path.isEmpty()) return false;
    const juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .getChildFile("muClid_samples");
    return tempDir.exists() && juce::File(path).isAChildOf(tempDir);
}

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
// The writer and reader are serialiseModulators / deserialiseModulators (ModulatorSerialise.h).

juce::ValueTree buildLayerTree(const Rhythm& r, const juce::AudioProcessorValueTreeState& apvts, int slot,
                               const juce::String& samplePath, const juce::File& samplesDir,
                               bool embedSample, bool forFile, const juce::String& presetName,
                               const juce::String& category, const juce::String& description)
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

    if (forFile)
    {
        state.setProperty("r0_name",   juce::String(r.name), nullptr);
        state.setProperty("r0_colour", r.colourIndex,        nullptr);
    }
    state.setProperty("r0_sample",
                      embedSample
                          ? juce::String()
                          : toRelativeSamplePath(samplePath, samplesDir),
                      nullptr);

    // Rhythm presets store ONLY sequencer-page state (Euclidean params, voice chain,
    // envelopes, insert effect). Mixer-page state (channel level/pan/sends/sidechain/
    // output bus) is not part of a layer preset (.muRhythm); the strip is not carried by it.
    const juce::String srcPrefix = "r" + juce::String(slot) + "_";
    for (int i = 0; i < kRhythmParamCount; ++i)
        if (auto* param = apvts.getParameter(srcPrefix + kRhythmParamDefs[i].suffix))
            writeParamPropertyV2(state,
                                 "r0_" + juce::String(kRhythmParamDefs[i].suffix),
                                 *param,
                                 kRhythmParamDefs[i]);

    // serialise modulators (ControlSequences + ModulationMatrix assignments).
    state.addChild(serialiseModulators(r), -1, nullptr);

    if (embedSample)
        embedSampleAt(state, samplePath);
    return state;
}

juce::ValueTree buildFullPresetNode(const Rhythm& r, const juce::AudioProcessorValueTreeState& apvts, int slot,
                                    const juce::String& samplePath, const juce::File& samplesDir,
                                    bool embedSamples, bool forSession)
{
    juce::ValueTree rTree("Rhythm");
    rTree.setProperty("name",   juce::String(r.name), nullptr);
    rTree.setProperty("colour", r.colourIndex,         nullptr);
    // A preset drops a temp-dir path (the embedded sampleData below carries the bytes); a
    // session keeps it, as the decoded file outlives the project reload.
    rTree.setProperty("sample",
                      isEmbeddedSampleTempPath(samplePath)
                          ? (forSession ? samplePath : juce::String())
                          : toRelativeSamplePath(samplePath, samplesDir),
                      nullptr);

    // v2 writes per the param's ParamKind — actual values for floats, ints, bools as
    // "true"/"false", algorithm selectors as the stable name string.
    const juce::String srcPrefix = "r" + juce::String(slot) + "_";
    for (int j = 0; j < kRhythmParamCount; ++j)
        if (auto* param = apvts.getParameter(srcPrefix + kRhythmParamDefs[j].suffix))
            writeParamPropertyV2(rTree,
                                 juce::String(kRhythmParamDefs[j].suffix),
                                 *param,
                                 kRhythmParamDefs[j]);

    const juce::String chSrcPrefix = "ch" + juce::String(slot) + "_";
    for (int j = 0; kChannelSuffixes[j] != nullptr; ++j)
        if (auto* param = apvts.getParameter(chSrcPrefix + kChannelSuffixes[j]))
            rTree.setProperty("ch_" + juce::String(kChannelSuffixes[j]), param->getValue(), nullptr);

    // serialise modulators per rhythm.
    rTree.addChild(serialiseModulators(r), -1, nullptr);
    writeRhythmClips(rTree, r, forSession);

    if (embedSamples)
        embedSampleAt(rTree, samplePath);
    return rTree;
}

juce::ValueTree buildGlobalState(const juce::AudioProcessorValueTreeState& apvts)
{
    // eff_algo / rev_algo / mst_insChar / mst_ins2Char emit their stable algorithm
    // name string; bool params emit "true"/"false"; the rest emit actual values.
    juce::ValueTree globalTree("GlobalState");
    for (int i = 0; i < mu_clid::kGlobalParamDefCount; ++i)
    {
        const auto& def = mu_clid::kGlobalParamDefs[i];
        if (auto* param = apvts.getParameter(def.id))
        {
            const float actual = param->convertFrom0to1(param->getValue());
            writeKindedProperty(globalTree, juce::String(def.id), actual, def.kind, def.algorithmNames);
        }
    }
    return globalTree;
}

void restoreChannelStrip(juce::AudioProcessorValueTreeState& apvts, int slot, const juce::ValueTree& rTree)
{
    const juce::String dstChPrefix = "ch" + juce::String(slot) + "_";
    for (int j = 0; kChannelSuffixes[j] != nullptr; ++j)
    {
        juce::Identifier chPropId { "ch_" + juce::String(kChannelSuffixes[j]) };
        if (rTree.hasProperty(chPropId))
            if (auto* param = apvts.getParameter(dstChPrefix + kChannelSuffixes[j]))
                param->setValueNotifyingHost((float)rTree.getProperty(chPropId));
    }
}

std::optional<juce::String> loadPresetSample(VoiceEngine& voice, const juce::ValueTree& tree,
                                             const juce::String& samplePathProp, const juce::String& sampleDataProp,
                                             const juce::String& sampleNameProp, const RhythmPresetContext& ctx,
                                             bool reportMissing, const juce::String& context,
                                             const juce::String& defaultSampleName)
{
    // Embedded bytes first (decoded to a temp file), else the stored path; then the file is loaded into `voice`.
    const auto smp = resolvePresetSample(tree.getProperty(juce::Identifier(sampleDataProp)).toString(),
                                         tree.getProperty(juce::Identifier(sampleNameProp), defaultSampleName).toString(),
                                         tree.getProperty(juce::Identifier(samplePathProp)).toString(),
                                         ctx.samplesDir,
                                         tree.getProperty(juce::Identifier(sampleDataProp + "Codec")).toString(),
                                         (int) tree.getProperty(juce::Identifier(sampleDataProp + "Bits"), 0));
    if (smp.kind == PresetSample::Kind::BadEmbed) return std::nullopt;   // unreadable embedded data: leave the voice as it is
    return applyPresetSample(smp, voice, ctx.onLoadError, reportMissing, context);
}

std::optional<juce::String> restoreSample(VoiceEngine& voice, const juce::ValueTree& tree, int slot,
                                          const juce::String& samplePathProp, const juce::String& sampleDataProp,
                                          const juce::String& sampleNameProp, const RhythmPresetContext& ctx)
{
    return loadPresetSample(voice, tree, samplePathProp, sampleDataProp, sampleNameProp, ctx, false,
                            " (rhythm " + juce::String(slot + 1) + ")");
}

void restoreGlobalState(juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& root)
{
    // GlobalState child was added later; older files omit it, which leaves every global where it
    // was. Iterate kGlobalParamDefs so algorithm-selector params (eff_algo / rev_algo /
    // mst_insChar / mst_ins2Char) use the name-string lookup path in v2.
    // v3 migration: collapse master-insert named fields → insP1..4 (algo-aware).
    // Mutates a copy so the original child is untouched.
    juce::ValueTree migrated = root.getChildWithName("GlobalState").createCopy();
    if (! migrated.isValid()) migrated = juce::ValueTree("GlobalState");
    migrateMasterInsertSlotsV3(migrated, 1);
    migrateMasterInsertSlotsV3(migrated, 2);

    // A file saved before the reverb Decay knob: the Decay its algorithm had then (the saved
    // algorithm, or the current one when the file leaves rev_algo alone), never the last preset's.
    if (! migrated.hasProperty(mu_pp::kReverbDecayId))
        if (auto* revAlgo = apvts.getParameter(mu_pp::kReverbAlgoId))
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
        if (auto* param = apvts.getParameter(def.id))
        {
            const float actualVal = readGlobalPropertyAsActual(migrated, juce::String(def.id),
                                                               *param, def);
            if (! std::isnan(actualVal))
                param->setValueNotifyingHost(param->convertTo0to1(actualVal));
        }
    }
}

// One rhythm's data from a preset node: a .muRhythm root (properties prefixed "r0_") or a .muClid <Rhythm>
// child (unprefixed). It starts from a default Rhythm, so whatever the node doesn't carry is the default (as in
// every product), then takes the node's identity and modulators. No file work: the sample is loadPresetSample's.
Rhythm readRhythm(const juce::ValueTree& nodeIn, const juce::String& prefix,
                  const RhythmPresetContext& ctx, const juce::String& source)
{
    Rhythm r;
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
        if (const auto* found = findRhythmParamDef(def.suffix))
            found->apply(actualVal, r, pd, vd);
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
        if (! dropped.isEmpty() && ctx.onLoadError)
            ctx.onLoadError("Dropped " + juce::String(dropped.size()) + " modulator assignment(s) from "
                            + source + ": " + dropped.joinIntoString("; "));
    }

    return r;
}

} // namespace mu_clid::rhythm_preset
