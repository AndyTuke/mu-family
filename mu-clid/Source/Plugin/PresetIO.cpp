#include "PresetIO.h"
#include "Plugin/PluginProcessor.h"
#include "Plugin/PluginProcessor_Internal.h"
#include "Persistence/PresetFiles.h"        // mu_pp::safePresetFileName + replaceFileAtomically

namespace mu_clid {

// mu-Clid shares only the file-name rule with mu-core's PresetFiles helpers. Its on-disk
// format predates them and must keep loading: presetName / presetCategory properties on
// the root (not name / category), per-rhythm values written as actual kinded values
// (not normalised <p> rows), embedded sample data, atomic writes, and a categories.txt
// list that keeps user categories even when no preset uses them yet. The rhythm's own
// trees are written and read by Persistence/RhythmPresetTree.

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

using rhythm_preset::isEmbeddedSampleTempPath;

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
PreparedRhythm PresetIO::prepareRhythmPresetTree(const juce::ValueTree& state, int rhythmIndex,
                                                           bool keepIdentity, const juce::String& source)
{
    if (! state.isValid()) return {};
    auto prepared = SampleRhythm::prepareStaged(state, "r0_", presetContext(), source);
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
    return rhythm_preset::buildLayerTree(proc.sequencer.getRhythm(rhythmIndex), proc.apvts, rhythmIndex,
                                         proc.samples.path(rhythmIndex), proc.getSamplesDir(),
                                         embedSample, forFile, presetName, category, description);
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
    proc.suspendProcessing(true);
    {
        const juce::ScopedLock sl(proc.layersLock);
        proc.hotSwapStager.installRhythm(targetIndex, std::move(prepared.rhythm), std::move(prepared.voice),
                                          prepared.samplePath, 0, RhythmSequencer::kAnyFrameEpoch);
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
        root.addChild(rhythm_preset::buildFullPresetNode(proc.sequencer.getRhythm(i), proc.apvts, i,
                                                         proc.samples.path(i), proc.getSamplesDir(),
                                                         embedSamples, forSession),
                      -1, nullptr);

    // Save global FX/mixer state so a preset fully restores the session.
    root.addChild(rhythm_preset::buildGlobalState(proc.apvts), -1, nullptr);

    return root;
}

// ── PresetIO::loadPreset helpers ────────────────────────────────────────────
//
// loadPreset itself is a sequence of named steps; each step lives in one of the
// helpers below. All run on the message thread under a single
// mu_core::ScopedApvtsLoading guard scoped to the caller.

// The context a rhythm tree is read in: the processor's Samples folder, prepared rate and block size.
RhythmPresetContext PresetIO::presetContext() const
{
    return { proc.getSamplesDir(), proc.currentSampleRate, proc.currentBlockSize, proc.onLoadError };
}

void PresetIO::restoreRhythmSample(int i, const juce::ValueTree& tree,
                                    const juce::String& samplePathProp,
                                    const juce::String& sampleDataProp,
                                    const juce::String& sampleNameProp)
{
    // A slot without an engine has nothing to load a sample into. (Mirrors the null guard
    // in forceSyncRhythmFromApvts.)
    if (! proc.voiceEngine(i)) return;

    if (const auto path = rhythm_preset::restoreSample(*proc.voiceEngine(i), tree, i, samplePathProp,
                                                       sampleDataProp, sampleNameProp, presetContext()))
        proc.samples.setPath(i, *path);
}

// Build the whole prepared payload from a parsed MuClidPreset root (at most `maxRhythms` rhythms:
// an unlicensed build activates no more than its demo cap).
static HotSwapStager::PreparedFullPreset
buildPreparedFullPreset(const juce::ValueTree& root, const RhythmPresetContext& ctx, int maxRhythms)
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
        auto prepared = SampleRhythm::prepareStaged(rTree, {}, ctx, "rhythm " + juce::String(i + 1));
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
    return buildPreparedFullPreset(root, presetContext(), (int) RhythmSequencer::MaxRhythms);
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
        // Only this product's own state root is legacy host state; another product's file is rejected.
        if (root.hasType(proc.apvts.state.getType()))
        {
            restoreStateFromTree(root);
            return true;
        }
        proc.noteFullPresetLoadFailed();
        if (proc.onLoadError) proc.onLoadError("Not a mu-Clid preset: " + file.getFileName());
        return false;
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
    // Engines that must not destruct under the lock (the shrunk slots') land here and free after it is released.
    std::array<std::unique_ptr<VoiceEngine>, RhythmSequencer::MaxRhythms> shrunkEngines;
    proc.suspendProcessing(true);
    {
        const juce::ScopedLock lw(proc.layoutWriteLock);   // first: the count stores below advance the layout serial
        const juce::ScopedLock sl(proc.layersLock);

    proc.setRhythmCount(n);

    if (n < oldN)
    {
        proc.storeLayerCount(n, std::memory_order_release);  // shrink: drop count first
        proc.retireBin.cutAtOrAbove(n);                       // the dropped slots' tails stop (count policy CutAtCount)
    }

    for (int i = 0; i < n; ++i)
    {
        // The shared install: the outgoing engine retires and keeps rendering its tail while the
        // new preset plays, instead of being hard-cut. Grown slots have no old engine.
        proc.hotSwapStager.installRhythm(i, std::move(prepared.rhythms[(size_t) i]),
                                          std::move(prepared.voices[(size_t) i]), prepared.samplePaths[(size_t) i],
                                          originEff, frameEpoch);
        // Prepare MIDI engines for freshly-grown slots; existing slots keep theirs.
        if (i >= oldN && proc.currentSampleRate > 0 && proc.currentBlockSize > 0)
            proc.sampleRhythm(i).midi.prepare(proc.currentSampleRate, proc.currentBlockSize);
    }

    // Tear down slots that are no longer active (shrink case). The voice engine is parked for a free
    // after the lock; the MIDI engine and mixer channel are plain values / atomics that the audio thread
    // reads, so they reset here without freeing anything; the sample path is message-thread only and is
    // cleared after the lock, so its string free never sits under it.
    for (int i = n; i < oldN; ++i)
    {
        shrunkEngines[(size_t) i] = std::move(proc.voiceEngine(i));
        proc.parkPendingMidiOff(proc.sampleRhythm(i).midi);   // keep the owed note-off; the audio thread no longer visits this slot
        proc.sampleRhythm(i).midi = MidiOutputEngine{};
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
        rhythm_preset::restoreChannelStrip(proc.apvts, i, rTree);
    }
    rhythm_preset::restoreGlobalState(proc.apvts, root);
}

} // namespace mu_clid
