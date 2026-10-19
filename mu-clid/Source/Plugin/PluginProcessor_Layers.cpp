// PluginProcessor — the layer container hooks. ProcessorBase owns the rhythms as layers and runs the
// structural edits (add, delete, reorder, reset); what mu-Clid keeps per slot on the processor side
// (voice and MIDI engines, retired tails, play state, sample paths) is moved here, in the order the
// edit's locked section always had. Partial-class TU split from PluginProcessor.cpp.

#include "Audio/SpinLock.h"
#include "Persistence/ScopedApvtsLoading.h"
#include "PluginProcessor.h"
#include "Sequencer/GeneratorAliases.h"
#include "Sequencer/SampleRhythm.h"

#include <algorithm>

namespace mu_clid {

// A default rhythm named for its slot, as mu-Clid's layer type. The palette colour follows the slot, as at
// construction; an add replaces it with the first colour no live rhythm uses.
std::unique_ptr<Layer> PluginProcessor::makeRhythm(int slot) const
{
    auto r = std::make_unique<SampleRhythm>();
    r->name        = ("Rhythm " + juce::String(slot + 1)).toStdString();
    r->colourIndex = slot % MuLookAndFeel::kLayerPaletteSize;
    return r;
}

// Message thread, outside every lock: the voice engine a slot needs once it is live.
std::unique_ptr<VoiceEngine> PluginProcessor::makeSlotEngine(int slot) const
{
    auto engine = std::make_unique<VoiceEngine>();
    engine->insertProc.setSlotIndex(slot);   // per-slot noise seed
    engine->prepareToPlay(currentSampleRate, currentBlockSize);
    return engine;
}

// ProcessorBase builds a layer before it takes any lock: for an add (a slot at or above the live count)
// that is also the moment to build the slot's engines; a remove's spare and a reset's fresh layer
// are for slots that are already live and keep their engines.
std::unique_ptr<Layer> PluginProcessor::createLayer(int slot)
{
    const bool adding = slot >= numLayers.load(std::memory_order_relaxed);
    std::unique_ptr<VoiceEngine> engine;
    if (adding)
    {
        engine = makeSlotEngine(slot);
        samples.clearPath(slot);   // the new slot carries no sample
    }
    auto layer = makeRhythm(slot);
    if (adding)
    {
        auto& fresh = static_cast<SampleRhythm&>(*layer);
        fresh.voice = std::move(engine);
        fresh.midi.prepare(currentSampleRate, currentBlockSize);   // the new rhythm's MIDI engine

        // The tails a count-reducing load left in the slot stay with the slot, as they did when the processor
        // held them: the new rhythm takes them over from the spare it replaces (which is not live, so no block
        // renders it meanwhile).
        auto& spare = sampleRhythm(slot);
        for (int k = 0; k < kMaxRetiredEngines; ++k)
        {
            fresh.retiredEngines[(size_t) k] = std::move(spare.retiredEngines[(size_t) k]);
            fresh.retiredReadyForCleanup[(size_t) k].store(spare.retiredReadyForCleanup[(size_t) k].load(std::memory_order_acquire),
                                                           std::memory_order_release);
            spare.retiredReadyForCleanup[(size_t) k].store(false, std::memory_order_release);
        }
    }
    return layer;
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
    setLiveRhythms(n);
}

void PluginProcessor::setLiveRhythms(int n)
{
    n = juce::jlimit(0, RhythmSequencer::MaxRhythms, n);
    const int current = sequencer.getNumRhythms();
    for (int i = std::min(n, current); i < std::max(n, current); ++i)
        sampleRhythm(i).cursor.clear();
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
        if (auto& engine = voiceEngine(firstIndex))
            engine->releaseHeldNote();
    }

    setLiveRhythms(numLayers.load(std::memory_order_relaxed));
    for (int i = 0; i < RhythmSequencer::MaxRhythms; ++i)
        sequencer.bindRhythm(i, static_cast<Rhythm*>(layers[(size_t) i].get()));

    // A slot's tenant changed, so the modulation change-detection starts clean: the next block re-runs a
    // modulated tenant's pass and pattern recompute instead of trusting the previous tenant's memory.
    for (int i = firstIndex; i <= lastIndex && i < RhythmSequencer::MaxRhythms; ++i)
        sampleRhythm(i).resetModulationMemory();
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
        sampleRhythm(a).cursor.requestAbsorb();   // each cursor moved with its rhythm: absorb the current step
        sampleRhythm(b).cursor.requestAbsorb();
        resetPlayState(a);
        resetPlayState(b);
        samples.swapPaths(a, b);

        // The voice engines moved with their rhythms. The retired tails stay in their slot's channel on a swap,
        // so the two layers hand theirs back (nothing is allocated or freed).
        auto& ra = sampleRhythm(a);
        auto& rb = sampleRhythm(b);
        for (int k = 0; k < kMaxRetiredEngines; ++k)
        {
            std::swap(ra.retiredEngines[(size_t) k], rb.retiredEngines[(size_t) k]);
            const bool flagA = ra.retiredReadyForCleanup[(size_t) k].load(std::memory_order_acquire);
            ra.retiredReadyForCleanup[(size_t) k].store(rb.retiredReadyForCleanup[(size_t) k].load(std::memory_order_acquire),
                                                        std::memory_order_release);
            rb.retiredReadyForCleanup[(size_t) k].store(flagA, std::memory_order_release);
        }
        return;
    }

    // Remove slot `a`: everything above it moved down one, each rhythm with its voice and retired engines (a
    // retired tail renders into the channel of the slot that holds it, so it follows the rhythm whose swap left
    // it); the removed rhythm's go with it, destroyed by the base after the unlock, and the vacated top slot's
    // spare holds none.
    const int n    = sequencer.getNumRhythms();   // the count before the removal
    const int newN = n - 1;
    reorderLastSlot = newN;

    sequencer.removeSlotState(a);
    for (int i = a; i < newN; ++i)
        sampleRhythm(i).cursor.requestAbsorb();   // the rhythms that moved down absorb the current step
    samples.shiftPathsDown(a, n);

    // A moved engine takes its new slot's noise seed.
    for (int i = a; i < newN; ++i)
    {
        auto& r = sampleRhythm(i);
        if (r.voice) r.voice->insertProc.setSlotIndex(i);
        for (auto& tail : r.retiredEngines)
            if (tail) tail->insertProc.setSlotIndex(i);
    }
}

// Under layoutWriteLock and layersLock, before layersReordered(Remove): the removed rhythm's MIDI engine
// may still owe a note-off (its 20 ms hold); it goes to the host's orphan list, drained by the next block.
void PluginProcessor::layerRemoved(Layer& removed, int index)
{
    juce::ignoreUnused(index);
    parkPendingMidiOff(static_cast<SampleRhythm&>(removed).midi);
}

//==============================================================================
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
        updateRhythmPattern(slot);
        if (voiceEngine(slot))
            voiceEngine(slot)->setParams(getRhythm(slot).voiceParams);
    }

    if (slot == reorderLastSlot)
        reorderLastSlot = -1;
}

} // namespace mu_clid
