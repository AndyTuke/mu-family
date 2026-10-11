#include "Sequencer/MidiRhythmCore.h"
#include "Plugin/ParameterLayout.h"
#include "Audio/SpinLock.h"                      // mu-core: spin lock helpers
#include "Persistence/ScopedApvtsLoading.h"      // mu-core: raises apvtsLoading for a bulk push
#include "Persistence/RhythmParamTable.h"        // mu-Clid: kRhythmParamDefs, the shared suffix table
#include "Persistence/SessionState.h"
#include "Sequencer/AccentVelocity.h"

#include <cmath>
#include <cstring>

namespace mu_clid_midi {

// Suffix lookup on raw characters: parameterChanged can run on the audio thread, where building a
// juce::String for the suffix would allocate.
static const mu_clid::RhythmParamDef* findDef(const char* suffix) noexcept
{
    for (int i = 0; i < mu_clid::kRhythmParamCount; ++i)
        if (std::strcmp(suffix, mu_clid::kRhythmParamDefs[i].suffix) == 0)
            return &mu_clid::kRhythmParamDefs[i];
    return nullptr;
}

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

    pushRhythmToApvts();
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
    // The message thread holds this while it edits the rhythm; skip the block rather than wait,
    // but still let a pending note-off out.
    const juce::ScopedTryLock lock(rhythmsLock);
    if (! lock.isLocked())
    {
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
        const Rhythm& rhy = sequencer.getRhythm(0);
        rhythmPlayState.stepsA.store(juce::jmax(1, rhy.genA.steps));
        rhythmPlayState.stepsB.store(juce::jmax(1, rhy.genB.steps));
        rhythmPlayState.stepsC.store(juce::jmax(1, rhy.genC.steps));
    }
    flushPendingNoteOff(midi, numSamples);
}

//==============================================================================
bool MidiRhythmCore::restoreSession(const juce::String& xml, juce::String& error)
{
    bool ok = false;
    const juce::ScopedLock sl(rhythmsLock);
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
        syncRhythmParam(raw + prefixLen, v);
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

// Queues the change in its suffix slot; on the message thread it is applied at once.
void MidiRhythmCore::syncRhythmParam(const char* suffix, float v)
{
    const auto* def = findDef(suffix);
    if (def == nullptr) return;

    auto& slot = pendingParams[(size_t) (def - mu_clid::kRhythmParamDefs)];
    slot.value.store(v, std::memory_order_relaxed);
    slot.dirty.store(true, std::memory_order_release);

    if (isMessageThread())
        drainPendingParams();
}

// Message thread. Applies every queued change and the pattern rebuild together, so the generator
// fields never change under a rebuild. Lock order: rhythmsLock, voiceParamsLock, pattern lock.
void MidiRhythmCore::drainPendingParams()
{
    bool any = false;
    for (auto& slot : pendingParams)
        if (slot.dirty.load(std::memory_order_relaxed)) { any = true; break; }
    if (! any) return;

    // The audio thread only try-locks this, so it skips a block rather than reading a half-edit.
    const juce::ScopedLock sl(rhythmsLock);
    Rhythm& r = sequencer.getRhythm(0);
    bool patternDirty = false;
    bool voiceDirty   = false;

    mu_core::spinLock(r.voiceParamsLock);
    for (int i = 0; i < mu_clid::kRhythmParamCount; ++i)
        if (pendingParams[(size_t) i].dirty.exchange(false, std::memory_order_acquire))
            mu_clid::kRhythmParamDefs[i].apply(pendingParams[(size_t) i].value.load(std::memory_order_relaxed),
                                               r, patternDirty, voiceDirty);

    // The rebuild reads the fields just written, so it stays inside the same voiceParamsLock hold.
    if (patternDirty)
    {
        if (apvtsLoading.load(std::memory_order_acquire))
            patternRebuildPending.store(true, std::memory_order_release);
        else
            sequencer.updatePattern(0);
    }
    mu_core::spinUnlock(r.voiceParamsLock);
}

void MidiRhythmCore::servicePendingRebuild()
{
    drainPendingParams();

    if (! apvtsLoading.load(std::memory_order_acquire)
        && patternRebuildPending.exchange(false, std::memory_order_acq_rel))
    {
        const juce::ScopedLock sl(rhythmsLock);
        Rhythm& r = sequencer.getRhythm(0);
        mu_core::spinLock(r.voiceParamsLock);
        sequencer.updatePattern(0);
        mu_core::spinUnlock(r.voiceParamsLock);
    }
}

// Rebuilds the rhythm from the current parameter values, whether or not parameterChanged fired.
void MidiRhythmCore::syncRhythmFromApvts()
{
    // The parameters are the truth now: drop queued changes (they would apply stale values), and clear
    // the rebuild request first so a change that lands during this rebuild survives for the next one.
    patternRebuildPending.store(false, std::memory_order_release);
    for (auto& slot : pendingParams)
        slot.dirty.store(false, std::memory_order_release);

    const juce::ScopedLock sl(rhythmsLock);
    Rhythm& r = sequencer.getRhythm(0);
    const juce::String prefix(kRhythmIdPrefix);
    bool patternDirty = false;
    bool voiceDirty   = false;

    // One suffix at a time, so the audio thread can take its brief lock between the writes.
    for (int j = 0; j < mu_clid::kRhythmParamCount; ++j)
        if (auto* raw = apvts.getRawParameterValue(prefix + mu_clid::kRhythmParamDefs[j].suffix))
        {
            mu_core::spinLock(r.voiceParamsLock);
            mu_clid::kRhythmParamDefs[j].apply(raw->load(), r, patternDirty, voiceDirty);
            mu_core::spinUnlock(r.voiceParamsLock);
        }

    sequencer.updatePattern(0);
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
