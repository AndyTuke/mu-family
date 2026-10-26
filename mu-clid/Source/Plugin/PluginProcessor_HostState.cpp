// PluginProcessor — host-state I/O: the DAW project / plugin state serialise and restore. A session is the
// .muClid full-preset tree (+ rows for the parameters it doesn't carry); sessions saved before that (the APVTS
// dump + r{i}_ properties) still restore through restoreStateFromTree. Partial-class TU split from
// PluginProcessor_Preset.cpp, which keeps the per-rhythm load helpers and loadPresetFile.
#include "Plugin/PluginProcessor.h"
#include "Plugin/PluginProcessor_Internal.h"
#include "Persistence/ModulatorSerialise.h" // serialiseModulators, deserialiseModulators, clearModulators
#include "Persistence/PresetMigrations.h"   // migrateLegacyHostState
#include "UI/Components/MuLookAndFeel.h" // kLayerPaletteSize
#include "Persistence/PresetHelpers.h"   // kGlobalParamDefs
#include "Persistence/LegacyReverbDecay.h"   // addLegacyReverbDecayParam
#include "Persistence/RhythmParamTable.h"   // kRhythmParamDefs
#include <set>

namespace mu_clid {
using mu_clid::migrate::migrateLegacyHostState;

// A session's rows for every parameter its .muClid tree doesn't carry — the inactive rhythm
// slots' params and anything outside the preset tables — so a project restores every parameter.
static const juce::Identifier kSessionParamsTag { "SessionParams" };

static juce::ValueTree uncoveredParamRows(juce::AudioProcessor& processor, int numRhythms)
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
    for (int i = 0; i < mu_clid::kGlobalParamDefCount; ++i) covered.insert(mu_clid::kGlobalParamDefs[i].id);

    juce::ValueTree rows(kSessionParamsTag);
    for (auto* p : processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
            if (covered.count(rp->getParameterID()) == 0)
                mu_pp::appendParamRow(rows, *rp, rp->getParameterID());
    return rows;
}

// The host session: the same .muClid tree a full preset saves (temp-dir sample paths kept, no
// embedded data) plus the rows for every other parameter. Restored by the same prepare + commit
// as a full preset (restoreSession).
void PluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto root = buildFullPresetTree({}, {}, {}, false, true);
    root.appendChild(uncoveredParamRows(*this, getNumRhythms()), nullptr);
    saveLiveState(root);   // the session also keeps the Performance (path, current item, mode)
    juce::MemoryOutputStream(destData, true).writeString(root.toXmlString());
}

void PluginProcessor::restoreSession(const juce::ValueTree& root)
{
    // A host restore applies at once (the project is loading, not a live swap), through the
    // full-preset prepare + commit; then the rows for the parameters the tree doesn't carry.
    auto prepared = prepareFullPreset(root);
    commitStagedFullPreset(prepared);

    const auto rows = root.getChildWithName(kSessionParamsTag);
    for (int i = 0; i < rows.getNumChildren(); ++i)
    {
        const auto row = rows.getChild(i);
        if (auto* p = apvts.getParameter(row.getProperty("id").toString()))
        {
            float v = 0.0f;
            if (mu_pp::readRowValue(row, *p, v) && p->getValue() != v)
                p->setValueNotifyingHost(v);
        }
    }
}

void PluginProcessor::restoreStateFromTree(const juce::ValueTree& state)
{
    // Applied at once: anything staged while the transport played must not land on top of the restored state.
    hotSwapStager.cancelAllStaged();

    // A session in this format predates clips, so every bank empties. Done before the lock below:
    // freeing trees is not a microsecond job.
    for (int i = 0; i < mu_limits::kMaxLayers; ++i)
        clearLayerClips(i);

    int n = juce::jlimit(1, mu_limits::kMaxLayers,
                         (int)state.getProperty("numRhythms", 1));

    // Guard the live-state mutation below (sequencer resize, voiceEngine
    // rebuild, per-rhythm sample swaps + pattern rebuilds) with suspendProcessing +
    // layersLock, matching SampleLibrary::load / ProcessorBase::swapLayers / the prestaged commit.
    // Without it the audio thread can tear-read voiceEngines or a half-swapped sample
    // buffer when a host restores project state on a live plugin. suspendProcessing
    // alone is not enough — it doesn't block an in-flight processBlock; layersLock
    // does (the audio thread's ScopedTryLock bails while we hold it). The lock is held
    // for the rest of the function (RAII) and released on return.
    // The engines the restore drops and the ones it adds are freed and built out here, outside the locks: the
    // shrunk slots' engines land in `shrunkEngines` (declared before the locks, so they destruct after them) and
    // the grown slots' engines are built and prepared now.
    std::array<std::unique_ptr<VoiceEngine>, mu_limits::kMaxLayers> shrunkEngines;
    std::array<std::unique_ptr<VoiceEngine>, mu_limits::kMaxLayers> grownEngines;
    for (int i = numLayers.load(std::memory_order_acquire); i < n; ++i)
        grownEngines[(size_t) i] = makeSlotEngine(i);

    suspendProcessing(true);
    const juce::ScopedLock lw(layoutWriteLock);   // first: the count stores below advance the layout serial
    const juce::ScopedLock sl(layersLock);

    // Expand to MaxRhythms so parameterChanged can write to all 8 rhythm slots.
    setRhythmCount(mu_limits::kMaxLayers);

    // migrate legacy state in-place before pushing it into APVTS so the
    // new 0..10 s ADSR ranges don't clamp old 0..100 values to absurd attacks.
    juce::ValueTree migrated = state.createCopy();
    migrateLegacyHostState(migrated);
    mu_pp::addLegacyReverbDecayParam(migrated, apvts.getParameter(mu_pp::kReverbAlgoId));   // older than the Decay knob

    {
        mu_core::ScopedApvtsLoading guard(apvtsLoading);
        apvts.replaceState(migrated);
    }

    // Trim to actual active count.
    setRhythmCount(n);

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
        Rhythm& target = getRhythm(ri);
        clearModulators(target);
        auto dropped = deserialiseModulators(child, target);
        if (! dropped.isEmpty() && onLoadError)
            onLoadError("Dropped " + juce::String(dropped.size())
                        + " modulator assignment(s) on rhythm " + juce::String(ri + 1)
                        + ": " + dropped.joinIntoString("; "));
    }

    // Populate fixed voice/midi arrays to match n.
    // Ordering matters: when shrinking, store the new (smaller) count BEFORE destroying
    // excess slots so the audio thread can't access a slot being reset.  When expanding,
    // create and prepare slots BEFORE incrementing the count so the audio thread never
    // sees an uninitialised slot.
    const int oldN = numLayers.load(std::memory_order_acquire);
    if (n < oldN)
    {
        storeLayerCount(n, std::memory_order_release);  // decrement first
        retireBin.cutAtOrAbove(n);                       // the dropped slots' tails stop (count policy CutAtCount)
        for (int i = n; i < oldN; ++i)
        {
            shrunkEngines[(size_t) i] = std::move(voiceEngine(i));
            parkPendingMidiOff(sampleRhythm(i).midi);   // keep the owed note-off (under layersLock, held above)
            sampleRhythm(i).midi = MidiOutputEngine{};
        }
    }
    else
    {
        for (int i = oldN; i < n; ++i)
        {
            // Built above with its slot's noise seed; a slot the pre-lock count did not foresee is built here.
            if (grownEngines[(size_t) i] == nullptr)
                grownEngines[(size_t) i] = makeSlotEngine(i);
            voiceEngine(i) = std::move(grownEngines[(size_t) i]);
            if (currentSampleRate > 0 && currentBlockSize > 0)
                sampleRhythm(i).midi.prepare(currentSampleRate, currentBlockSize);
        }
        storeLayerCount(n, std::memory_order_release);  // increment after slots ready
    }

    // Restore non-APVTS properties and refresh engines.
    for (int i = 0; i < n; ++i)
    {
        Rhythm& r = getRhythm(i);
        const juce::String slotPrefix = "r" + juce::String(i) + "_";

        r.name        = state.getProperty(slotPrefix + "name",
                                          "Rhythm " + juce::String(i + 1)).toString().toStdString();
        r.colourIndex = (int)state.getProperty(slotPrefix + "colour", i % MuLookAndFeel::kLayerPaletteSize);

        // force-sync APVTS → Rhythm so shrink/grow cycles (preset A → B → A)
        // repopulate freshly-defaulted Rhythm fields even when JUCE skips listener
        // callbacks because the APVTS values didn't change. Internally calls
        // updatePattern + voiceEngine(i)->setParams.
        forceSyncRhythmFromApvts(i);

        // Host-state format prefixes every sample-related property with "r{i}_".
        samples.setPath(i, state.getProperty(slotPrefix + "sample").toString());
        restoreRhythmSample(i, state,
                             slotPrefix + "sample",
                             slotPrefix + "sampleData",
                             slotPrefix + "sampleName");
    }

    suspendProcessing(false);   // layersLock (sl) releases on return
}

void PluginProcessor::restoreHostState(const void* data, int sizeInBytes)
{
    // Parsed first: a session that left Performance mode on must win over `_default`, because the live
    // state names the performance item that was playing, and that is the session's, not the default's.
    const auto xml = juce::parseXML(juce::String::fromUTF8((const char*)data, sizeInBytes));
    const bool performing = xml != nullptr && mu_core::LiveState::modeSaved(juce::ValueTree::fromXml(*xml));

    // in standalone, the user's saved `_default.muClid` is authoritative
    // on every launch — JUCE's auto-saved "filterState" should NOT override it.
    // The host (DAW) path still needs setStateInformation to restore project
    // state, so this override only fires when running standalone.
    if (wrapperType == juce::AudioProcessor::wrapperType_Standalone && ! ProcessorBase::skipAutoLoadDefault && ! performing)
    {
        const juce::File defaultPreset = getPresetsDir().getChildFile("_default.muClid");
        if (defaultPreset.existsAsFile())
        {
            loadPresetFile(defaultPreset);
            return;
        }
        // No default preset saved — fall through to JUCE's auto-restore.
    }

    if (xml != nullptr)
    {
        // Sessions are .muClid trees; older ones (the APVTS dump + r{i}_ properties) keep their reader.
        auto state = juce::ValueTree::fromXml(*xml);

        // A host restore applies at once, for either format: anything staged while the transport played
        // must not land on top of it. Runs on the message thread, like the rest of the restore.
        if (state.isValid())
            hotSwapStager.cancelAllStaged();

        if (state.hasType("MuClidPreset"))
            restoreSession(state);
        else if (state.hasType(apvts.state.getType()))
            restoreStateFromTree(state);
        else if (state.isValid())
        {
            noteFullPresetLoadFailed();
            if (onLoadError)
                onLoadError("Host state restore failed: not a mu-Clid state");
        }
        else if (onLoadError)
            onLoadError("Host state restore failed: invalid tree");
        if (state.isValid())
            restoreLiveState(state);   // after the preset: it names the item that is playing
    }
    else if (onLoadError)
    {
        onLoadError("Host state restore failed: could not parse XML");
    }
}

} // namespace mu_clid
