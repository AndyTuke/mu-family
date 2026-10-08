// PresetIO host-state I/O — DAW project / plugin-state serialise + restore. A session is the
// .muClid full-preset tree (+ rows for the parameters it doesn't carry); sessions saved before
// that (the APVTS dump + r{i}_ properties) still restore through restoreStateFromTree.
//
// Partial class: these are PresetIO members declared in PresetIO.h, split out
// of PresetIO.cpp so the preset save / .muClid load path and the
// host-state path each read as their own file. This TU owns the host-state
// format version, the message-thread state tree builder, the legacy-state
// migration, and the three AudioProcessor state overrides. The per-rhythm load
// helpers (restoreRhythmSample etc.) and loadPreset stay in PresetIO.cpp and
// are reached here as ordinary cross-TU method calls.
#include "PresetIO.h"
#include "PluginProcessor.h"
#include "PluginProcessor_Internal.h"
#include "Persistence/ModulatorSerialise.h" // serialiseModulators, deserialiseModulators, clearModulators
#include "Persistence/PresetMigrations.h"   // migrateLegacyHostState
#include "UI/Components/MuLookAndFeel.h" // kChannelPaletteSize
#include "Persistence/PresetHelpers.h"   // kGlobalParamDefs
#include "Persistence/RhythmParamTable.h"   // kRhythmParamDefs
#include <set>

using mu_pp::serialiseModulators;
using mu_pp::deserialiseModulators;
using mu_pp::clearModulators;
using mu_pp_migrate::migrateLegacyHostState;
using mu_pp::kRhythmParamDefs;
using mu_pp::kRhythmParamCount;
using mu_pp::kChannelSuffixes;

// A session's rows for every parameter its .muClid tree doesn't carry — the inactive rhythm
// slots' params and anything outside the preset tables — so a project restores every parameter.
static const juce::Identifier kSessionParamsTag { "SessionParams" };

static juce::ValueTree uncoveredParamRows(juce::AudioProcessor& proc, int numRhythms)
{
    // The ids the .muClid tree already writes: each active rhythm's table params + mixer strip,
    // and the global defs.
    std::set<juce::String> covered;
    for (int i = 0; i < numRhythms; ++i)
    {
        const juce::String r = "r" + juce::String(i) + "_", ch = "ch" + juce::String(i) + "_";
        for (int j = 0; j < kRhythmParamCount; ++j)  covered.insert(r + kRhythmParamDefs[j].suffix);
        for (int j = 0; kChannelSuffixes[j] != nullptr; ++j) covered.insert(ch + kChannelSuffixes[j]);
    }
    for (int i = 0; i < mu_pp::kGlobalParamDefCount; ++i) covered.insert(mu_pp::kGlobalParamDefs[i].id);

    juce::ValueTree rows(kSessionParamsTag);
    for (auto* p : proc.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
            if (covered.count(rp->getParameterID()) == 0)
                mu_pp::appendParamRow(rows, *rp, rp->getParameterID());
    return rows;
}

// The host session: the same .muClid tree a full preset saves (temp-dir sample paths kept, no
// embedded data) plus the rows for every other parameter. Restored by the same prepare + commit
// as a full preset (restoreSession).
void PresetIO::getStateInformation(juce::MemoryBlock& destData)
{
    auto root = buildFullPresetTree({}, {}, {}, false, true);
    root.appendChild(uncoveredParamRows(proc_, proc_.sequencer.getNumRhythms()), nullptr);
    juce::MemoryOutputStream(destData, true).writeString(root.toXmlString());
}

void PresetIO::restoreSession(const juce::ValueTree& root)
{
    // A host restore applies at once (the project is loading, not a live swap), through the
    // full-preset prepare + commit; then the rows for the parameters the tree doesn't carry.
    auto prepared = prepareFullPreset(root);
    commitStagedFullPreset(prepared);

    const auto rows = root.getChildWithName(kSessionParamsTag);
    for (int i = 0; i < rows.getNumChildren(); ++i)
    {
        const auto row = rows.getChild(i);
        if (auto* p = proc_.apvts.getParameter(row.getProperty("id").toString()))
        {
            float v = 0.0f;
            if (mu_pp::readRowValue(row, *p, v) && p->getValue() != v)
                p->setValueNotifyingHost(v);
        }
    }
}

void PresetIO::restoreStateFromTree(const juce::ValueTree& state)
{
    int n = juce::jlimit(1, SequencerEngine::MaxRhythms,
                         (int)state.getProperty("numRhythms", 1));

    // Demo cap: an unlicensed build activates at most demoMaxChannels() rhythms. The
    // rest of the preset's params still load into APVTS but stay inactive — identical
    // to the normal "smaller preset" shrink path, so no extra teardown is needed.
    if (! proc_.isLicensed())
        n = juce::jmin(n, proc_.demoMaxChannels());

    // Guard the live-state mutation below (sequencer resize, voiceEngine
    // rebuild, per-rhythm sample swaps + pattern rebuilds) with suspendProcessing +
    // rhythmsLock, matching SampleLibrary::load / RhythmManager::swap / the prestaged commit.
    // Without it the audio thread can tear-read voiceEngines or a half-swapped sample
    // buffer when a host restores project state on a live plugin. suspendProcessing
    // alone is not enough — it doesn't block an in-flight processBlock; rhythmsLock
    // does (the audio thread's ScopedTryLock bails while we hold it). The lock is held
    // for the rest of the function (RAII) and released on return.
    proc_.suspendProcessing(true);
    const juce::ScopedLock sl(proc_.rhythmsLock);

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
    // Each child carries a rhythmIndex property so we apply to the right slot
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
        const juce::String slotPrefix = "r" + juce::String(i) + "_";

        r.name        = state.getProperty(slotPrefix + "name",
                                          "Rhythm " + juce::String(i + 1)).toString().toStdString();
        r.colourIndex = (int)state.getProperty(slotPrefix + "colour", i % MuLookAndFeel::kChannelPaletteSize);

        // force-sync APVTS → Rhythm so shrink/grow cycles (preset A → B → A)
        // repopulate freshly-defaulted Rhythm fields even when JUCE skips listener
        // callbacks because the APVTS values didn't change. Internally calls
        // updatePattern + proc_.voiceEngines[i]->setParams.
        proc_.forceSyncRhythmFromAPVTS(i);

        // Host-state format prefixes every sample-related property with "r{i}_".
        proc_.samples.setPath(i, state.getProperty(slotPrefix + "sample").toString());
        restoreRhythmSample(i, state,
                             slotPrefix + "sample",
                             slotPrefix + "sampleData",
                             slotPrefix + "sampleName");
    }

    proc_.suspendProcessing(false);   // rhythmsLock (sl) releases on return
}

void PresetIO::setStateInformation(const void* data, int sizeInBytes)
{
    // in standalone, the user's saved `_default.muClid` is authoritative
    // on every launch — JUCE's auto-saved "filterState" should NOT override it.
    // The host (DAW) path still needs setStateInformation to restore project
    // state, so this override only fires when running standalone.
    if (proc_.wrapperType == juce::AudioProcessor::wrapperType_Standalone && ! ProcessorBase::skipAutoLoadDefault)
    {
        const juce::File defaultPreset = proc_.getPresetsDir().getChildFile("_default.muClid");
        if (defaultPreset.existsAsFile())
        {
            loadPreset(defaultPreset);
            return;
        }
        // No default preset saved — fall through to JUCE's auto-restore.
    }

    if (auto xml = juce::parseXML(juce::String::fromUTF8((const char*)data, sizeInBytes)))
    {
        // Sessions are .muClid trees; older ones (the APVTS dump + r{i}_ properties) keep their reader.
        auto state = juce::ValueTree::fromXml(*xml);
        if (state.hasType("MuClidPreset"))
            restoreSession(state);
        else if (state.isValid())
            restoreStateFromTree(state);
        else if (proc_.onLoadError)
            proc_.onLoadError("Host state restore failed: invalid tree");
    }
    else if (proc_.onLoadError)
    {
        proc_.onLoadError("Host state restore failed: could not parse XML");
    }
}
