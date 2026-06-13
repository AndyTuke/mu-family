// PresetIO host-state I/O — DAW project / plugin-state serialise + restore.
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
#include "Persistence/PresetMigrations.h"   // kCurrentStateFormatVersion, migrateLegacyHostState
#include "UI/Components/MuLookAndFeel.h" // kChannelPaletteSize

using mu_pp::serialiseModulators;
using mu_pp::deserialiseModulators;
using mu_pp::clearModulators;
using mu_pp_migrate::kCurrentStateFormatVersion;
using mu_pp_migrate::migrateLegacyHostState;

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
    // rhythmsLock, matching loadSampleForRhythm / swapRhythms / the prestaged commit.
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
        proc_.loadedSamplePaths.set(i, state.getProperty(slotPrefix + "sample").toString());
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
    if (proc_.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
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
