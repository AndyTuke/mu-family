// RhythmManager — rhythm-slot add / remove / swap / reset / rename (message thread).

#include "RhythmManager.h"
#include "Plugin/PluginProcessor.h"
#include "Plugin/PluginProcessor_Internal.h"   // mu_core::ScopedApvtsLoading
#include "Sequencer/GeneratorAliases.h"

namespace mu_clid {

void RhythmManager::add(const Rhythm& r)
{
    int ri = proc.sequencer.getNumRhythms();
    if (ri >= SequencerEngine::MaxRhythms) return;
    proc.hotSwapStager.cancelPendingIfAny(ri);          // the new slot must carry no stale staged swap
    proc.voiceEngines[ri] = std::make_unique<VoiceEngine>();
    proc.voiceEngines[ri]->insertProc.setSlotIndex(ri);   // per-slot noise seed
    proc.voiceEngines[ri]->prepareToPlay(proc.currentSampleRate, proc.currentBlockSize);
    proc.midiEngines[ri].prepare(proc.currentSampleRate, proc.currentBlockSize);
    {
        const juce::ScopedLock sl(proc.rhythmsLock);
        proc.sequencer.addRhythm(r);
        proc.numActiveRhythms.store(proc.sequencer.getNumRhythms(), std::memory_order_release);
    }
    proc.samples.clearPath(ri);
    {
        mu_core::ScopedApvtsLoading guard(proc.apvtsLoading);
        proc.pushRhythmToApvts(ri);
    }
}

void RhythmManager::resetPlayState(int idx)
{
    if (idx < 0 || idx >= SequencerEngine::MaxRhythms) return;
    auto& s = proc.rhythmPlayState[idx];
    s.currentStep  .store(0);
    s.patternLength.store(1);
    s.stepsA       .store(1);
    s.stepsB       .store(1);
    s.stepsC       .store(1);
    s.hitCount     .store(0);
}

bool RhythmManager::swap(int i, int j)
{
    const int n = proc.numActiveRhythms.load(std::memory_order_acquire);
    if (i < 0 || j < 0 || i >= n || j >= n || i == j) return false;

    // Pending per-rhythm staged swaps are keyed by index; swapping the slots would
    // misdirect them, so drop both. (A staged full preset is index-independent — it
    // replaces every slot at commit — so it is left to win.)
    proc.hotSwapStager.cancelPendingIfAny(i);
    proc.hotSwapStager.cancelPendingIfAny(j);

    // suspendProcessing only sets a flag — it does NOT block the in-flight
    // processBlock callback (see rhythmsLock in PluginProcessor.h). Take rhythmsLock to
    // serialise with the audio thread, the same way remove() and
    // runDeferredWork do. The audio thread's ScopedTryLock in processBlock will
    // bail (silent block) while we hold the lock — clean atomic swap.
    proc.suspendProcessing(true);
    {
        const juce::ScopedLock sl(proc.rhythmsLock);

        proc.sequencer.swapRhythmSlots(i, j);
        std::swap(proc.voiceEngines[i], proc.voiceEngines[j]);
        std::swap(proc.midiEngines[i],  proc.midiEngines[j]);
        resetPlayState(i);
        resetPlayState(j);

        proc.samples.swapPaths(i, j);

        // Re-translate sidechain source indices (channel and return strips) BEFORE the channel
        // swap, so any strip referring to the swapped slots keeps pointing at the same logical
        // rhythm after the swap.
        proc.mixerEngine.renumberSidechainsAfterSwap(i, j);

        proc.mixerEngine.swapChannelState(i, j);

        // Reset envelope follower state on both moved slots so the previous tenant's
        // ducking envelope doesn't bleed into the freshly arrived rhythm.
        proc.mixerEngine.resetSidechainEnv(i);
        proc.mixerEngine.resetSidechainEnv(j);
    }
    proc.suspendProcessing(false);

    proc.swapApvtsForRhythms(i, j);
    return true;
}

void RhythmManager::remove(int index)
{
    if (index < 0 || index >= proc.sequencer.getNumRhythms()) return;
    const int newN = proc.sequencer.getNumRhythms() - 1;

    // The down-shift renumbers every rhythm from `index` upward, so any pending
    // per-rhythm staged swap (keyed by index) would land on the wrong slot — drop
    // them all. A staged full preset is index-independent, so it is left to win.
    for (int r = 0; r < SequencerEngine::MaxRhythms; ++r)
        proc.hotSwapStager.cancelPendingIfAny(r);

    // The removed slot's retired tails are dropped; declared before the lock so they destruct after it.
    std::array<std::unique_ptr<VoiceEngine>, mu_limits::kMaxRetiredVoiceEngines> droppedTails;

    // suspendProcessing ensures no in-progress processBlock holds a stale
    // numActiveRhythms snapshot and reads rhythms[r] while we erase and shift.
    proc.suspendProcessing(true);
    {
        const juce::ScopedLock sl(proc.rhythmsLock);
        proc.numActiveRhythms.store(newN, std::memory_order_release);
        proc.sequencer.removeRhythm(index);
        proc.parkPendingMidiOff(index);   // the removed slot's engine is overwritten below; keep its owed note-off
        // The strips above shift down, so re-point every sidechain source first: a strip that ducked
        // from the deleted rhythm now ducks from nothing; the rest follow their rhythm down.
        proc.mixerEngine.renumberSidechainsAfterRemove(index);
        for (int i = index; i < newN; ++i)
        {
            proc.voiceEngines[i] = std::move(proc.voiceEngines[i + 1]);
            if (proc.voiceEngines[i]) proc.voiceEngines[i]->insertProc.setSlotIndex(i);   // the moved engine takes its new slot's noise seed
            proc.midiEngines[i]  = std::move(proc.midiEngines[i + 1]);
            proc.mixerEngine.channels[i].copyFrom(proc.mixerEngine.channels[i + 1]);
        }

        // A retired tail renders into the channel of the slot that holds it, so each slot's tails (and
        // their drained flags) move down with the rhythm whose swap left them; the removed slot's tails
        // are dropped and the vacated top slot is left empty.
        constexpr int K = mu_limits::kMaxRetiredVoiceEngines;
        for (int k = 0; k < K; ++k)
            droppedTails[(size_t) k] = std::move(proc.retiredVoiceEngines[(size_t) index][(size_t) k]);
        for (int i = index; i < newN; ++i)
            for (int k = 0; k < K; ++k)
            {
                auto& dst = proc.retiredVoiceEngines[(size_t) i][(size_t) k];
                dst = std::move(proc.retiredVoiceEngines[(size_t) i + 1][(size_t) k]);
                if (dst) dst->insertProc.setSlotIndex(i);
                proc.retiredReadyForCleanup[(size_t) i][(size_t) k].store(
                    proc.retiredReadyForCleanup[(size_t) i + 1][(size_t) k].load(std::memory_order_acquire),
                    std::memory_order_release);
            }
        for (int k = 0; k < K; ++k)
            proc.retiredReadyForCleanup[(size_t) newN][(size_t) k].store(false, std::memory_order_release);
        proc.voiceEngines[newN].reset();
        proc.midiEngines[newN] = MidiOutputEngine{};
        proc.mixerEngine.channels[newN].reset();
    }
    proc.suspendProcessing(false);

    // The rhythms and strips above `index` moved down a slot: bring their APVTS values with them, or
    // the parameters keep the old tenant's numbers and the next APVTS-driven resync would restore them.
    //
    // These pushes stay OUTSIDE the suspended section and rhythmsLock on purpose, like every other
    // commit path (swap, hot-swap, preset load). pushRhythmToApvts / pushMixerChannelToApvts call
    // setValueNotifyingHost about 80 times per slot (up to 7 slots), and each call runs the host's own
    // parameter-change code on this thread. Inside the lock that would turn every audio block into a
    // silent try-lock miss for the whole push, tens of milliseconds, and let host code run while the
    // lock is held. Outside, the stale window is only those pushes on the message thread: the audio
    // thread never reads these APVTS values (the engine and the Rhythm are already correct and
    // apvtsLoading mutes the listeners), so the only exposure is a host or UI read of a shifted
    // slot's parameter in that window, which sees the old tenant's number until its push lands.
    {
        mu_core::ScopedApvtsLoading guard(proc.apvtsLoading);
        for (int i = index; i < newN; ++i)
        {
            proc.pushRhythmToApvts(i);
            proc.pushMixerChannelToApvts(i);
        }
    }
}

void RhythmManager::reset(int index)
{
    if (index < 0 || index >= proc.sequencer.getNumRhythms()) return;
    proc.hotSwapStager.cancelPendingIfAny(index);   // a staged swap would re-fill what we're clearing

    // was a UI-thread spin on rhythm.modLock + concurrent vector destruction
    // risk in ModulationMatrix::process. Now uses the same suspendProcessing +
    // rhythmsLock pattern as remove() — clean atomic swap, no UI freeze even
    // if the audio thread is preempted while holding modLock.
    proc.suspendProcessing(true);
    {
        const juce::ScopedLock sl(proc.rhythmsLock);
        auto& r = proc.sequencer.getRhythm(index);
        auto savedName   = r.name;
        auto savedColour = r.colourIndex;
        r = Rhythm{};
        r.name        = savedName;
        r.colourIndex = savedColour;
    }
    proc.suspendProcessing(false);

    proc.sequencer.updatePattern(index);

    // The reset Rhythm is the truth: hand its voice parameters to the engine and its values to APVTS,
    // otherwise the engine keeps the old sound and the parameters keep the old numbers.
    if (proc.voiceEngines[index]) proc.voiceEngines[index]->setParams(proc.sequencer.getRhythm(index).voiceParams);
    mu_core::ScopedApvtsLoading guard(proc.apvtsLoading);
    proc.pushRhythmToApvts(index);
}

void RhythmManager::rename(int index, const juce::String& newName)
{
    if (index < 0 || index >= proc.sequencer.getNumRhythms()) return;

    // rhythmsLock serialises with the audio thread's ScopedTryLock in
    // processBlock. No audio-thread reader of name today, but the lock matches the
    // project's "message thread mutates Rhythm" convention and stays correct if a
    // future audio-path consumer (e.g. MIDI program-change preset matcher) reads it.
    const juce::ScopedLock sl(proc.rhythmsLock);
    proc.sequencer.getRhythm(index).name = newName.toStdString();
}

} // namespace mu_clid
