// PluginProcessor — the layer container hooks. ProcessorBase owns the rhythms as layers and runs the
// structural edits (add, delete, reorder, reset); what mu-Clid keeps per slot on the processor side
// (voice and MIDI engines, retired tails, play state, sample paths) is moved here, in the order the
// edit's locked section always had. Partial-class TU split from PluginProcessor.cpp.

#include "Audio/SpinLock.h"
#include "Persistence/ScopedApvtsLoading.h"
#include "PluginProcessor.h"
#include "Sequencer/GeneratorAliases.h"

#include <algorithm>

namespace mu_clid {

// A default rhythm named for its slot. The palette colour follows the slot, as at construction; an add
// replaces it with the first colour no live rhythm uses.
std::unique_ptr<Layer> PluginProcessor::makeRhythm(int slot) const
{
    auto r = std::make_unique<Rhythm>();
    r->name        = ("Rhythm " + juce::String(slot + 1)).toStdString();
    r->colourIndex = slot % MuLookAndFeel::kLayerPaletteSize;
    return r;
}

// Message thread, outside every lock: the engines a slot needs once it is live.
void PluginProcessor::prepareSlotEngines(int slot)
{
    voiceEngines[(size_t) slot] = std::make_unique<VoiceEngine>();
    voiceEngines[(size_t) slot]->insertProc.setSlotIndex(slot);   // per-slot noise seed
    voiceEngines[(size_t) slot]->prepareToPlay(currentSampleRate, currentBlockSize);
    midiEngines[(size_t) slot].prepare(currentSampleRate, currentBlockSize);
    samples.clearPath(slot);                                      // the new slot carries no sample
}

// ProcessorBase builds a layer before it takes any lock: for an add (a slot at or above the live count)
// that is also the moment to build the slot's engines; a remove's spare and a reset's fresh layer
// are for slots that are already live and keep their engines.
std::unique_ptr<Layer> PluginProcessor::createLayer(int slot)
{
    // Every edit that parks engines under the lock builds a layer first, so anything an earlier edit
    // left parked is freed here, and layersReordered never frees one under the lock.
    releaseParkedEngines();
    if (slot >= numLayers.load(std::memory_order_relaxed))
        prepareSlotEngines(slot);
    return makeRhythm(slot);
}

void PluginProcessor::resetPlayState(int idx)
{
    if (idx < 0 || idx >= RhythmSequencer::MaxRhythms) return;
    auto& s = rhythmPlayState[(size_t) idx];
    s.patternLength.store(1);
    s.stepsA       .store(1);
    s.stepsB       .store(1);
    s.stepsC       .store(1);
    s.hitCount     .store(0);
}

// Sets the sequencer's live rhythm count. A rhythm that stops being live is returned to a default one, so
// a later grow starts from defaults (a restore then fills in what the state carries).
void PluginProcessor::setRhythmCount(int n)
{
    n = juce::jlimit(0, RhythmSequencer::MaxRhythms, n);
    for (int i = n; i < sequencer.getNumRhythms(); ++i)
        layerAs<Rhythm>(i) = Rhythm{};
    sequencer.setNumRhythms(n);
}

void PluginProcessor::renameRhythm(int index, const juce::String& newName)
{
    if (index < 0 || index >= getNumRhythms()) return;

    // layersLock serialises with the audio thread's try-lock in processBlock. Nothing on the audio
    // path reads the name today, but the lock keeps this correct if a consumer (a MIDI program-change
    // preset matcher, say) ever does.
    const juce::ScopedLock sl(layersLock);
    getRhythm(index).name = newName.toStdString();
}

//==============================================================================
// Called under layersLock after every pointer move: the sequencer's count and Rhythm bindings follow
// the layer set. Nothing is built or freed here.
void PluginProcessor::layersMoved(int firstIndex, int lastIndex)
{
    // An edit that did not reorder (add, reset) rebuilds the new rhythm's pattern after the unlock.
    if (reorderLastSlot < 0)
    {
        rebuildSlot = firstIndex;

        // A reset slot keeps its engine, whose last hit would otherwise sit at the envelope's sustain
        // until the next trigger. Done here, under the lock and before the new pattern exists, so a
        // fresh hit can never be released; nothing is allocated or freed.
        if (auto& engine = voiceEngines[(size_t) firstIndex])
            engine->releaseHeldNote();
    }

    sequencer.setNumRhythms(numLayers.load(std::memory_order_relaxed));
    for (int i = 0; i < RhythmSequencer::MaxRhythms; ++i)
        sequencer.bindRhythm(i, static_cast<Rhythm*>(layers[(size_t) i].get()));

    // A slot's tenant changed, so the modulation change-detection starts clean: the next block re-runs a
    // modulated tenant's pass and pattern recompute instead of trusting the previous tenant's memory.
    for (int i = firstIndex; i <= lastIndex && i < RhythmSequencer::MaxRhythms; ++i)
    {
        prevMatrixHadAssignments[(size_t) i] = false;
        prevEuclidOverrides[(size_t) i] = EuclidOverrides{};
        lastEuclidOverrides[(size_t) i] = EuclidOverrides{};
    }
}

// Called under layersLock, after the base moved the layer pointers and strip state and before
// layersMoved. The data at a slot came from elsewhere, so what the processor keeps per slot follows.
void PluginProcessor::layersReordered(LayerEdit kind, int a, int b)
{
    rebuildSlot = -1;

    if (kind == LayerEdit::Swap)
    {
        reorderLastSlot = b;
        sequencer.swapSlotState(a, b);
        std::swap(voiceEngines[(size_t) a], voiceEngines[(size_t) b]);
        std::swap(midiEngines[(size_t) a],  midiEngines[(size_t) b]);
        resetPlayState(a);
        resetPlayState(b);
        samples.swapPaths(a, b);
        return;
    }

    // Remove slot `a`: everything above it moves down one.
    const int n    = sequencer.getNumRhythms();   // the count before the removal
    const int newN = n - 1;
    reorderLastSlot = newN;
    jassert(std::all_of(parkedEngines.begin(), parkedEngines.end(), [](const auto& p) { return p == nullptr; }));

    sequencer.removeSlotState(a);
    parkPendingMidiOff(a);   // the removed slot's engine is overwritten below; keep its owed note-off
    samples.shiftPathsDown(a, n);

    parkedEngines[0] = std::move(voiceEngines[(size_t) a]);
    for (int i = a; i < newN; ++i)
    {
        voiceEngines[(size_t) i] = std::move(voiceEngines[(size_t) i + 1]);
        if (voiceEngines[(size_t) i]) voiceEngines[(size_t) i]->insertProc.setSlotIndex(i);   // the moved engine takes its new slot's noise seed
        midiEngines[(size_t) i] = std::move(midiEngines[(size_t) i + 1]);
    }

    // A retired tail renders into the channel of the slot that holds it, so each slot's tails (and
    // their drained flags) move down with the rhythm whose swap left them; the removed slot's tails
    // are parked and the vacated top slot is left empty.
    constexpr int K = kMaxRetiredEngines;
    for (int k = 0; k < K; ++k)
        parkedEngines[(size_t) (1 + k)] = std::move(retiredVoiceEngines[(size_t) a][(size_t) k]);
    for (int i = a; i < newN; ++i)
        for (int k = 0; k < K; ++k)
        {
            auto& dst = retiredVoiceEngines[(size_t) i][(size_t) k];
            dst = std::move(retiredVoiceEngines[(size_t) i + 1][(size_t) k]);
            if (dst) dst->insertProc.setSlotIndex(i);
            retiredReadyForCleanup[(size_t) i][(size_t) k].store(
                retiredReadyForCleanup[(size_t) i + 1][(size_t) k].load(std::memory_order_acquire),
                std::memory_order_release);
        }
    for (int k = 0; k < K; ++k)
        retiredReadyForCleanup[(size_t) newN][(size_t) k].store(false, std::memory_order_release);
    midiEngines[(size_t) newN] = MidiOutputEngine{};
}

//==============================================================================
// Frees what a remove parked, after the lock is dropped.
void PluginProcessor::releaseParkedEngines()
{
    for (auto& e : parkedEngines)
        e.reset();
}

// Message thread, processing suspended, not under layersLock: brings the parameters (and the pieces
// that depend on the rhythm's data) in step with the slot's rhythm and strip. The pushes stay outside
// the lock on purpose: each setValueNotifyingHost runs the host's own parameter-change code, and inside
// the lock every audio block would miss its try-lock for the whole push. The audio thread never reads
// these parameter values, and apvtsLoading mutes the listeners meanwhile.
void PluginProcessor::syncSlotParams(int slot)
{
    const bool live = slot < sequencer.getNumRhythms();
    {
        mu_core::ScopedApvtsLoading guard(apvtsLoading);
        if (live) pushRhythmToApvts(slot);
        pushMixerChannelToApvts(slot);
    }

    if (! live)
        samples.clearPath(slot);   // the vacated top slot: the removed rhythm's path was moved up here

    // A new or reset rhythm: its pattern is built now, and the engine takes its voice parameters. A
    // moved rhythm keeps both, and rebuilding its pattern would make the sequencer re-absorb the step.
    if (slot == rebuildSlot)
    {
        rebuildSlot = -1;
        sequencer.updatePattern(slot);
        if (voiceEngines[(size_t) slot])
        {
            voiceEngines[(size_t) slot]->setParams(getRhythm(slot).voiceParams);
        }
    }

    if (slot == reorderLastSlot)
    {
        reorderLastSlot = -1;
        releaseParkedEngines();
    }
}

} // namespace mu_clid
