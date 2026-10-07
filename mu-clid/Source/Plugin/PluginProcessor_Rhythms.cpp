// PluginProcessor — rhythm management (message thread): add, swap, remove, reset, rename,
// MIDI note mode. Partial-class TU split from PluginProcessor.cpp.

#include "PluginProcessor.h"
#include "PluginProcessor_Internal.h"
#include "Audio/SpinLock.h"            // mu-core: spin lock helpers
#include "Audio/InsertSlotConfig.h"
#include "Plugin/ModulationSkew.h"     // proportion-space skew helpers (shared with test C5)
#include "Modulation/MuClidModDest.h"  // mu-clid modulation targets
#include "Sequencer/Rhythm.h"

void PluginProcessor::addRhythm(const Rhythm& r)
{
    int ri = sequencer.getNumRhythms();
    if (ri >= SequencerEngine::MaxRhythms) return;
    hotSwapStager.cancelPendingIfAny(ri);          // the new slot must carry no stale staged swap
    voiceEngines[ri] = std::make_unique<VoiceEngine>();
    voiceEngines[ri]->prepareToPlay(currentSampleRate, currentBlockSize);
    midiEngines[ri].prepare(currentSampleRate, currentBlockSize);
    {
        const juce::ScopedLock sl(rhythmsLock);
        sequencer.addRhythm(r);
        numActiveRhythms.store(sequencer.getNumRhythms(), std::memory_order_release);
    }
    if (ri < loadedSamplePaths.size())
        loadedSamplePaths.set(ri, juce::String());
    {
        mu_core::ScopedApvtsLoading guard(apvtsLoading);
        pushRhythmToAPVTS(ri);
    }
}

void PluginProcessor::resetPlayState(int idx)
{
    if (idx < 0 || idx >= SequencerEngine::MaxRhythms) return;
    auto& s = rhythmPlayState[idx];
    s.currentStep  .store(0);
    s.patternLength.store(1);
    s.stepsA       .store(1);
    s.stepsB       .store(1);
    s.stepsC       .store(1);
    s.hitCount     .store(0);
}
bool PluginProcessor::swapRhythms(int i, int j)
{
    const int n = numActiveRhythms.load(std::memory_order_acquire);
    if (i < 0 || j < 0 || i >= n || j >= n || i == j) return false;

    // Pending per-rhythm staged swaps are keyed by index; swapping the slots would
    // misdirect them, so drop both. (A staged full preset is index-independent — it
    // replaces every slot at commit — so it is left to win.)
    hotSwapStager.cancelPendingIfAny(i);
    hotSwapStager.cancelPendingIfAny(j);

    // suspendProcessing only sets a flag — it does NOT block the in-flight
    // processBlock callback (see PluginProcessor.h:266). Take rhythmsLock to
    // serialise with the audio thread, the same way removeRhythm and
    // handleAsyncUpdate do. The audio thread's ScopedTryLock in processBlock will
    // bail (silent block) while we hold the lock — clean atomic swap.
    suspendProcessing(true);
    {
        const juce::ScopedLock sl(rhythmsLock);

        sequencer.swapRhythmSlots(i, j);
        std::swap(voiceEngines[i], voiceEngines[j]);
        std::swap(midiEngines[i],  midiEngines[j]);
        resetPlayState(i);
        resetPlayState(j);

        juce::String tmp = loadedSamplePaths[i];
        loadedSamplePaths.set(i, loadedSamplePaths[j]);
        loadedSamplePaths.set(j, tmp);

        // Re-translate sidechain source indices BEFORE the channel swap, so any
        // channel referring to the swapped slots keeps pointing at the same logical
        // rhythm after the swap.
        for (int c = 0; c < n; ++c)
        {
            auto& src = mixerEngine.channels[c].sidechainSource;
            const int s = src.load(std::memory_order_relaxed);
            if      (s == i) src.store(j, std::memory_order_relaxed);
            else if (s == j) src.store(i, std::memory_order_relaxed);
        }

        mixerEngine.swapChannelState(i, j);

        // Reset envelope follower state on both moved slots so the previous tenant's
        // ducking envelope doesn't bleed into the freshly arrived rhythm.
        mixerEngine.resetSidechainEnv(i);
        mixerEngine.resetSidechainEnv(j);
    }
    suspendProcessing(false);

    swapAPVTSForRhythms(i, j);
    return true;
}

void PluginProcessor::removeRhythm(int index)
{
    if (index < 0 || index >= sequencer.getNumRhythms()) return;
    const int newN = sequencer.getNumRhythms() - 1;

    // The down-shift renumbers every rhythm from `index` upward, so any pending
    // per-rhythm staged swap (keyed by index) would land on the wrong slot — drop
    // them all. A staged full preset is index-independent, so it is left to win.
    for (int r = 0; r < SequencerEngine::MaxRhythms; ++r)
        hotSwapStager.cancelPendingIfAny(r);

    // suspendProcessing ensures no in-progress processBlock holds a stale
    // numActiveRhythms snapshot and reads rhythms[r] while we erase and shift.
    suspendProcessing(true);
    {
        const juce::ScopedLock sl(rhythmsLock);
        numActiveRhythms.store(newN, std::memory_order_release);
        sequencer.removeRhythm(index);
        for (int i = index; i < newN; ++i)
        {
            voiceEngines[i] = std::move(voiceEngines[i + 1]);
            midiEngines[i]  = std::move(midiEngines[i + 1]);
            mixerEngine.channels[i].copyFrom(mixerEngine.channels[i + 1]);
        }
        voiceEngines[newN].reset();
        midiEngines[newN] = MidiOutputEngine{};
        mixerEngine.channels[newN].reset();
    }
    suspendProcessing(false);
}

void PluginProcessor::resetRhythm(int index)
{
    if (index < 0 || index >= sequencer.getNumRhythms()) return;
    hotSwapStager.cancelPendingIfAny(index);   // a staged swap would re-fill what we're clearing

    // was a UI-thread spin on rhythm.modLock + concurrent vector destruction
    // risk in ModulationMatrix::process. Now uses the same suspendProcessing +
    // rhythmsLock pattern as removeRhythm — clean atomic swap, no UI freeze even
    // if the audio thread is preempted while holding modLock.
    suspendProcessing(true);
    {
        const juce::ScopedLock sl(rhythmsLock);
        auto& r = sequencer.getRhythm(index);
        auto savedName   = r.name;
        auto savedColour = r.colourIndex;
        r = Rhythm{};
        r.name        = savedName;
        r.colourIndex = savedColour;
    }
    suspendProcessing(false);

    sequencer.updatePattern(index);
}

void PluginProcessor::renameRhythm(int index, const juce::String& newName)
{
    if (index < 0 || index >= sequencer.getNumRhythms()) return;

    // rhythmsLock serialises with the audio thread's ScopedTryLock in
    // processBlock. No audio-thread reader of name today, but the lock matches the
    // project's "message thread mutates Rhythm" convention and stays correct if a
    // future audio-path consumer (e.g. MIDI program-change preset matcher) reads it.
    const juce::ScopedLock sl(rhythmsLock);
    sequencer.getRhythm(index).name = newName.toStdString();
}

//==============================================================================
void PluginProcessor::setMidiNoteMode(int mode)
{
    midiNoteMode.store(mode, std::memory_order_relaxed);
    if (mode == 0)
    {
        // Switching back to Free: clear any held-note state so the next Free-mode
        // block doesn't see stale noteModePlaying = true from a prior Note session.
        midiHeldNotes  .store(0,     std::memory_order_relaxed);
        noteModePlaying.store(false, std::memory_order_relaxed);
        noteModeBeatPos.store(0.0,   std::memory_order_relaxed);
    }
    appSettings->setValue("midiNoteMode", mode);
    appSettings->saveIfNeeded();
}
