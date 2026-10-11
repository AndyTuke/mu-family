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

// True on the message thread, and when no message manager exists (a headless run), where nothing
// else can be the audio thread.
static bool onMessageThread()
{
    const auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    return mm == nullptr || mm->isThisTheMessageThread();
}

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
    : apvts(apvtsToUse)
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

void MidiRhythmCore::syncRhythmParam(const char* suffix, float v)
{
    const auto* def = findDef(suffix);
    if (def == nullptr) return;

    Rhythm& r = sequencer.getRhythm(0);
    bool patternDirty = false;
    bool voiceDirty   = false;

    // Hold the voice-params lock for the field write so the audio thread cannot see a torn value.
    mu_core::spinLock(r.voiceParamsLock);
    def->apply(v, r, patternDirty, voiceDirty);
    mu_core::spinUnlock(r.voiceParamsLock);

    if (patternDirty && ! apvtsLoading.load(std::memory_order_acquire))
    {
        // updatePattern allocates and takes a spin lock, so only the message thread may run it.
        if (onMessageThread())
            sequencer.updatePattern(0);
        else
            patternRebuildPending.store(true, std::memory_order_release);
    }
}

void MidiRhythmCore::servicePendingRebuild()
{
    if (patternRebuildPending.exchange(false, std::memory_order_acq_rel)
        && ! apvtsLoading.load(std::memory_order_acquire))
        sequencer.updatePattern(0);
}

// Rebuilds the rhythm from the current parameter values, whether or not parameterChanged fired.
void MidiRhythmCore::syncRhythmFromApvts()
{
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
    patternRebuildPending.store(false, std::memory_order_release);   // this rebuild covers any pending one
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
