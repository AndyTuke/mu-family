// RhythmManager — rhythm-slot add / remove / swap / reset / rename (message thread).

#include "RhythmManager.h"
#include "Plugin/PluginProcessor.h"
#include "Plugin/PluginProcessor_Internal.h"   // mu_core::ScopedApvtsLoading
#include "Sequencer/Rhythm.h"

namespace mu_clid {

void RhythmManager::add(const Rhythm& r)
{
    int ri = proc_.sequencer.getNumRhythms();
    if (ri >= SequencerEngine::MaxRhythms) return;
    proc_.hotSwapStager.cancelPendingIfAny(ri);          // the new slot must carry no stale staged swap
    proc_.voiceEngines[ri] = std::make_unique<VoiceEngine>();
    proc_.voiceEngines[ri]->prepareToPlay(proc_.currentSampleRate, proc_.currentBlockSize);
    proc_.midiEngines[ri].prepare(proc_.currentSampleRate, proc_.currentBlockSize);
    {
        const juce::ScopedLock sl(proc_.rhythmsLock);
        proc_.sequencer.addRhythm(r);
        proc_.numActiveRhythms.store(proc_.sequencer.getNumRhythms(), std::memory_order_release);
    }
    proc_.samples.clearPath(ri);
    {
        mu_core::ScopedApvtsLoading guard(proc_.apvtsLoading);
        proc_.pushRhythmToAPVTS(ri);
    }
}

void RhythmManager::resetPlayState(int idx)
{
    if (idx < 0 || idx >= SequencerEngine::MaxRhythms) return;
    auto& s = proc_.rhythmPlayState[idx];
    s.currentStep  .store(0);
    s.patternLength.store(1);
    s.stepsA       .store(1);
    s.stepsB       .store(1);
    s.stepsC       .store(1);
    s.hitCount     .store(0);
}

bool RhythmManager::swap(int i, int j)
{
    const int n = proc_.numActiveRhythms.load(std::memory_order_acquire);
    if (i < 0 || j < 0 || i >= n || j >= n || i == j) return false;

    // Pending per-rhythm staged swaps are keyed by index; swapping the slots would
    // misdirect them, so drop both. (A staged full preset is index-independent — it
    // replaces every slot at commit — so it is left to win.)
    proc_.hotSwapStager.cancelPendingIfAny(i);
    proc_.hotSwapStager.cancelPendingIfAny(j);

    // suspendProcessing only sets a flag — it does NOT block the in-flight
    // processBlock callback (see rhythmsLock in PluginProcessor.h). Take rhythmsLock to
    // serialise with the audio thread, the same way remove() and
    // handleAsyncUpdate do. The audio thread's ScopedTryLock in processBlock will
    // bail (silent block) while we hold the lock — clean atomic swap.
    proc_.suspendProcessing(true);
    {
        const juce::ScopedLock sl(proc_.rhythmsLock);

        proc_.sequencer.swapRhythmSlots(i, j);
        std::swap(proc_.voiceEngines[i], proc_.voiceEngines[j]);
        std::swap(proc_.midiEngines[i],  proc_.midiEngines[j]);
        resetPlayState(i);
        resetPlayState(j);

        proc_.samples.swapPaths(i, j);

        // Re-translate sidechain source indices BEFORE the channel swap, so any
        // channel referring to the swapped slots keeps pointing at the same logical
        // rhythm after the swap.
        for (int c = 0; c < n; ++c)
        {
            auto& src = proc_.mixerEngine.channels[c].sidechainSource;
            const int s = src.load(std::memory_order_relaxed);
            if      (s == i) src.store(j, std::memory_order_relaxed);
            else if (s == j) src.store(i, std::memory_order_relaxed);
        }

        proc_.mixerEngine.swapChannelState(i, j);

        // Reset envelope follower state on both moved slots so the previous tenant's
        // ducking envelope doesn't bleed into the freshly arrived rhythm.
        proc_.mixerEngine.resetSidechainEnv(i);
        proc_.mixerEngine.resetSidechainEnv(j);
    }
    proc_.suspendProcessing(false);

    proc_.swapAPVTSForRhythms(i, j);
    return true;
}

void RhythmManager::remove(int index)
{
    if (index < 0 || index >= proc_.sequencer.getNumRhythms()) return;
    const int newN = proc_.sequencer.getNumRhythms() - 1;

    // The down-shift renumbers every rhythm from `index` upward, so any pending
    // per-rhythm staged swap (keyed by index) would land on the wrong slot — drop
    // them all. A staged full preset is index-independent, so it is left to win.
    for (int r = 0; r < SequencerEngine::MaxRhythms; ++r)
        proc_.hotSwapStager.cancelPendingIfAny(r);

    // suspendProcessing ensures no in-progress processBlock holds a stale
    // numActiveRhythms snapshot and reads rhythms[r] while we erase and shift.
    proc_.suspendProcessing(true);
    {
        const juce::ScopedLock sl(proc_.rhythmsLock);
        proc_.numActiveRhythms.store(newN, std::memory_order_release);
        proc_.sequencer.removeRhythm(index);
        for (int i = index; i < newN; ++i)
        {
            proc_.voiceEngines[i] = std::move(proc_.voiceEngines[i + 1]);
            proc_.midiEngines[i]  = std::move(proc_.midiEngines[i + 1]);
            proc_.mixerEngine.channels[i].copyFrom(proc_.mixerEngine.channels[i + 1]);
        }
        proc_.voiceEngines[newN].reset();
        proc_.midiEngines[newN] = MidiOutputEngine{};
        proc_.mixerEngine.channels[newN].reset();
    }
    proc_.suspendProcessing(false);
}

void RhythmManager::reset(int index)
{
    if (index < 0 || index >= proc_.sequencer.getNumRhythms()) return;
    proc_.hotSwapStager.cancelPendingIfAny(index);   // a staged swap would re-fill what we're clearing

    // was a UI-thread spin on rhythm.modLock + concurrent vector destruction
    // risk in ModulationMatrix::process. Now uses the same suspendProcessing +
    // rhythmsLock pattern as remove() — clean atomic swap, no UI freeze even
    // if the audio thread is preempted while holding modLock.
    proc_.suspendProcessing(true);
    {
        const juce::ScopedLock sl(proc_.rhythmsLock);
        auto& r = proc_.sequencer.getRhythm(index);
        auto savedName   = r.name;
        auto savedColour = r.colourIndex;
        r = Rhythm{};
        r.name        = savedName;
        r.colourIndex = savedColour;
    }
    proc_.suspendProcessing(false);

    proc_.sequencer.updatePattern(index);
}

void RhythmManager::rename(int index, const juce::String& newName)
{
    if (index < 0 || index >= proc_.sequencer.getNumRhythms()) return;

    // rhythmsLock serialises with the audio thread's ScopedTryLock in
    // processBlock. No audio-thread reader of name today, but the lock matches the
    // project's "message thread mutates Rhythm" convention and stays correct if a
    // future audio-path consumer (e.g. MIDI program-change preset matcher) reads it.
    const juce::ScopedLock sl(proc_.rhythmsLock);
    proc_.sequencer.getRhythm(index).name = newName.toStdString();
}

} // namespace mu_clid
