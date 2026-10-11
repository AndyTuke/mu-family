#include "Sequencer/MidiRhythmCore.h"
#include "Plugin/ParameterLayout.h"
#include "Audio/SpinLock.h"                      // mu-core: spin lock helpers
#include "Persistence/ScopedApvtsLoading.h"      // mu-core: raises apvtsLoading for a bulk push
#include "Persistence/RhythmParamTable.h"        // mu-Clid: kRhythmParamDefs, the shared suffix table
#include "Persistence/SessionState.h"
#include "Sequencer/AccentVelocity.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mu_clid_midi {

MidiRhythmCore::MidiRhythmCore(juce::AudioProcessorValueTreeState& apvtsToUse)
    : apvts(apvtsToUse), ownerThread(std::this_thread::get_id())
{
    sequencer.setStepCap(mu_core::HitGenerator::kMaxSteps);

    // Listen to every parameter: rhythm values rebuild the pattern, the master loop sets the loop.
    for (auto* param : apvts.processor.getParameters())
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
            apvts.addParameterListener(p->getParameterID(), this);

    // The one rhythm: 16 steps, 4 hits, and its values mirrored into the parameters.
    Rhythm defaultRhythm;
    defaultRhythm.name       = "<unnamed>";
    defaultRhythm.genA.steps = 16;
    defaultRhythm.genA.hits  = 4;
    sequencer.addRhythm(defaultRhythm);

    midiNotePtr  = apvts.getRawParameterValue(kMidiNoteId);
    accentAmtPtr = apvts.getRawParameterValue(kAccentAmtId);

    // Resolve each rhythm parameter once: its raw value pointer for the drain, and a sorted
    // suffix table so parameterChanged finds its slot without scanning the whole definition list.
    const juce::String prefix(kRhythmIdPrefix);
    for (int i = 0; i < mu_clid::kRhythmParamCount; ++i)
    {
        rawParams[(size_t) i] = apvts.getRawParameterValue(prefix + mu_clid::kRhythmParamDefs[i].suffix);
        if (rawParams[(size_t) i] != nullptr)
            suffixIndex[(size_t) suffixIndexCount++] = { mu_clid::kRhythmParamDefs[i].suffix, i };
    }
    std::sort(suffixIndex.begin(), suffixIndex.begin() + suffixIndexCount,
              [](const SuffixIndex& a, const SuffixIndex& b) { return std::strcmp(a.suffix, b.suffix) < 0; });

    pushRhythmToApvts();
    publishStepCounts();
}

MidiRhythmCore::~MidiRhythmCore()
{
    for (auto* param : apvts.processor.getParameters())
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
            apvts.removeParameterListener(p->getParameterID(), this);
}

void MidiRhythmCore::prepare(double sampleRate, int samplesPerBlock)
{
    midiEngine.prepare(sampleRate, samplesPerBlock);
}

//==============================================================================
void MidiRhythmCore::processBlock(bool playing, double beatPos, juce::MidiBuffer& midi, int numSamples)
{
    // A restore replaces the parameters, loop length and pattern as a set: skip the block rather than
    // play half of it, but still let a pending note-off out.
    if (restoreInProgress.load(std::memory_order_acquire))
    {
        skippedBlocks.fetch_add(1, std::memory_order_relaxed);
        flushPendingNoteOff(midi, numSamples);
        return;
    }

    advanceSequencer(playing, beatPos, midi, numSamples);
}

void MidiRhythmCore::flushPendingNoteOff(juce::MidiBuffer& midi, int numSamples)
{
    midiEngine.processBlock(midi, juce::jmax(1, numSamples));
}

void MidiRhythmCore::advanceSequencer(bool playing, double beatPos, juce::MidiBuffer& midi, int numSamples)
{
    if (playing)
    {
        const auto blockResult = sequencer.processBlock(beatPos);

        // Phase 1: a fired hit becomes one note-on, louder when the hit is accented.
        if (blockResult.firedMask & 1)
        {
            const int  midiNote = (int) midiNotePtr->load();
            const auto vel      = accentVelocities(accentAmtPtr->load());
            const bool accented = (blockResult.accentMask & 1) != 0;
            midiEngine.trigger(midi, 0, midiNote, 1, accented ? vel.accented : vel.normal);
            rhythmPlayState.hitCount.store(rhythmPlayState.hitCount.load() + 1);
        }

        // Phase 2: publish the play position for the editor's circle.
        beatFraction.store(static_cast<float>(std::fmod(beatPos / SequencerEngine::StepLengthBeats, 1.0)));
        rhythmPlayState.currentStep  .store(sequencer.getLastStepIndex(0));
        rhythmPlayState.currentStepC .store(sequencer.getLastAccentStepIndex(0));
        rhythmPlayState.patternLength.store(sequencer.getPatternLength(0));
        // The ring step counts are published by the message thread after each apply (publishStepCounts).
    }
    flushPendingNoteOff(midi, numSamples);
}

//==============================================================================
bool MidiRhythmCore::restoreSession(const juce::String& xml, juce::String& error)
{
    bool ok = false;
    const juce::ScopedLock sl(rhythmsLock);

    // Down again on every exit, including after the final rebuild below.
    struct RestoreFlag
    {
        explicit RestoreFlag(std::atomic<bool>& f) : flag(f) { flag.store(true, std::memory_order_release); }
        ~RestoreFlag() { flag.store(false, std::memory_order_release); }
        std::atomic<bool>& flag;
    } restoreFlag(restoreInProgress);
    {
        mu_core::ScopedApvtsLoading guard(apvtsLoading);
        session::Identity identity;
        ok = session::restoreFromXmlString(xml, apvts, error, &identity);

        // An old state's rhythm name and colour are not parameters, so they go onto the Rhythm here.
        if (ok)
        {
            Rhythm& r = sequencer.getRhythm(0);
            if (identity.name.isNotEmpty())  r.name        = identity.name.toStdString();
            if (identity.colourIndex >= 0)   r.colourIndex = identity.colourIndex;
        }
    }
    // JUCE skips listener callbacks for unchanged values, so rebuild the rhythm from the parameters.
    syncRhythmFromApvts();
    return ok;
}

//==============================================================================
void MidiRhythmCore::parameterChanged(const juce::String& id, float v)
{
    // Raw characters throughout: this can run on the audio thread, where String work would allocate.
    const char* raw = id.toRawUTF8();
    const size_t prefixLen = std::strlen(kRhythmIdPrefix);

    if (std::strncmp(raw, kRhythmIdPrefix, prefixLen) == 0)
    {
        syncRhythmParam(raw + prefixLen);
        return;
    }
    if (id == kMasterLoopId)
        sequencer.setMasterLoopSteps(juce::jmin((int) v * 16, 256));   // 0 = free
}

// True on the message thread. With no MessageManager (a headless run) it is the thread that built
// the core, so a test cannot silently take the message-thread path from its "audio" thread.
bool MidiRhythmCore::isMessageThread() const
{
    const auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    return mm != nullptr ? mm->isThisTheMessageThread() : std::this_thread::get_id() == ownerThread;
}

// Binary search on raw characters: parameterChanged can run on the audio thread, where building a
// juce::String for the suffix or scanning the whole definition list is not wanted. -1 if unknown.
int MidiRhythmCore::findParamIndex(const char* suffix) const noexcept
{
    const auto* begin = suffixIndex.data();
    const auto* end   = begin + suffixIndexCount;
    const auto* it = std::lower_bound(begin, end, suffix,
                                      [](const SuffixIndex& e, const char* s) { return std::strcmp(e.suffix, s) < 0; });
    return (it != end && std::strcmp(it->suffix, suffix) == 0) ? it->index : -1;
}

// Marks the suffix's slot dirty; on the message thread it is applied at once.
void MidiRhythmCore::syncRhythmParam(const char* suffix)
{
    const int index = findParamIndex(suffix);
    if (index < 0) return;

    pendingDirty[(size_t) index].store(true, std::memory_order_release);

    if (isMessageThread())
        drainPendingParams();
}

// Copies the ring step counts the circle shows out of the Rhythm for the UI, so the audio thread
// never reads the Rhythm. Caller holds rhythmsLock (or is the constructor).
void MidiRhythmCore::publishStepCounts()
{
    const Rhythm& r = sequencer.getRhythm(0);
    rhythmPlayState.stepsA.store(juce::jmax(1, r.genA.steps), std::memory_order_relaxed);
    rhythmPlayState.stepsB.store(juce::jmax(1, r.genB.steps), std::memory_order_relaxed);
    rhythmPlayState.stepsC.store(juce::jmax(1, r.genC.steps), std::memory_order_relaxed);
}

// Message thread. Applies every queued change and the pattern rebuild together, so the generator
// fields never change under a rebuild. Lock order: rhythmsLock, then the sequencer's pattern lock.
void MidiRhythmCore::drainPendingParams()
{
    bool any = false;
    for (auto& dirty : pendingDirty)
        if (dirty.load(std::memory_order_relaxed)) { any = true; break; }
    if (! any) return;

    // Serialises against a restore or another drain; the audio thread does not take this lock.
    const juce::ScopedLock sl(rhythmsLock);
    Rhythm& r = sequencer.getRhythm(0);
    bool patternDirty = false;
    bool voiceDirty   = false;

    // Clear the flag first, then read the parameter: a change that lands after the clear re-raises it.
    for (int i = 0; i < mu_clid::kRhythmParamCount; ++i)
        if (pendingDirty[(size_t) i].exchange(false, std::memory_order_acq_rel) && rawParams[(size_t) i] != nullptr)
            mu_clid::kRhythmParamDefs[i].apply(rawParams[(size_t) i]->load(), r, patternDirty, voiceDirty);

    if (patternDirty)
    {
        if (apvtsLoading.load(std::memory_order_acquire))
            patternRebuildPending.store(true, std::memory_order_release);
        else
            sequencer.updatePattern(0);
    }

    // After the rebuild, which clamps the step counts to the cap: the ring must show the clamped value.
    publishStepCounts();
}

void MidiRhythmCore::servicePendingRebuild()
{
    drainPendingParams();

    if (! apvtsLoading.load(std::memory_order_acquire)
        && patternRebuildPending.exchange(false, std::memory_order_acq_rel))
    {
        const juce::ScopedLock sl(rhythmsLock);
        sequencer.updatePattern(0);
        publishStepCounts();
    }
}

// Rebuilds the rhythm from the current parameter values, whether or not parameterChanged fired.
void MidiRhythmCore::syncRhythmFromApvts()
{
    // The parameters are the truth now: drop queued changes (they would apply stale values), and clear
    // the rebuild request first so a change that lands during this rebuild survives for the next one.
    patternRebuildPending.store(false, std::memory_order_release);
    for (auto& dirty : pendingDirty)
        dirty.store(false, std::memory_order_release);

    const juce::ScopedLock sl(rhythmsLock);
    Rhythm& r = sequencer.getRhythm(0);
    bool patternDirty = false;
    bool voiceDirty   = false;

    for (int j = 0; j < mu_clid::kRhythmParamCount; ++j)
        if (rawParams[(size_t) j] != nullptr)
            mu_clid::kRhythmParamDefs[j].apply(rawParams[(size_t) j]->load(), r, patternDirty, voiceDirty);

    sequencer.updatePattern(0);
    publishStepCounts();   // after the rebuild, which clamps the step counts to the cap
    sequencer.setMasterLoopSteps(juce::jmin((int) apvts.getRawParameterValue(kMasterLoopId)->load() * 16, 256));
}

// Mirrors the Rhythm's values into the parameters (used once, for the default rhythm).
void MidiRhythmCore::pushRhythmToApvts()
{
    mu_core::ScopedApvtsLoading guard(apvtsLoading);
    const Rhythm& r = sequencer.getRhythm(0);
    const juce::String prefix(kRhythmIdPrefix);

    for (int i = 0; i < mu_clid::kRhythmParamCount; ++i)
    {
        const auto& def = mu_clid::kRhythmParamDefs[i];
        if (auto* p = apvts.getParameter(prefix + def.suffix))
            p->setValueNotifyingHost(p->convertTo0to1(def.push(r)));
    }
}

} // namespace mu_clid_midi
