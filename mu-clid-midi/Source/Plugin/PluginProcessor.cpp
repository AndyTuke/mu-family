#include "Plugin/PluginProcessor.h"
#include "Plugin/ParameterLayout.h"
#include "Plugin/PluginEditor.h"
#include "Plugin/TransportResolver.h"            // mu-core: the family transport rule
#include "Persistence/ScopedApvtsLoading.h"      // mu-core: raises apvtsLoading for a bulk push
#include "Persistence/SessionState.h"
#include "Sequencer/AccentVelocity.h"

#include <cmath>

namespace mu_clid_midi {

// A MIDI effect: no audio buses at all.
PluginProcessor::PluginProcessor()
    : ProcessorBase(BusesProperties(), createParameterLayout(), juce::Identifier("MuClidState"))
{
    initAppSettings("muClid");   // settings file + saved UI size / MIDI clock (ProcessorBase)

    sequencer.setStepCap(mu_core::HitGenerator::kMaxSteps);

    // Listen to every parameter: rhythm values rebuild the pattern, the master loop sets the loop.
    for (auto* param : getParameters())
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

PluginProcessor::~PluginProcessor()
{
    for (auto* param : getParameters())
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
            apvts.removeParameterListener(p->getParameterID(), this);
}

//==============================================================================
// UTF-8 Greek lowercase mu (U+03BC) prefix, as everywhere else in the product's branding.
const juce::String PluginProcessor::getName() const
{
    return juce::String(juce::CharPointer_UTF8("\xce\xbc-Clid MIDI"));
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    midiEngine.prepare(sampleRate, samplesPerBlock);
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet()  == juce::AudioChannelSet::disabled()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::disabled();
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}

//==============================================================================
void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const auto transport = deriveTransport(numSamples);

    // The message thread holds this while it edits the rhythm; skip the block rather than wait.
    const juce::ScopedTryLock lock(rhythmsLock);
    if (! lock.isLocked()) return;

    advanceSequencer(transport.playing, transport.beatPos, midiMessages, numSamples);
}

PluginProcessor::BlockTransport PluginProcessor::deriveTransport(int numSamples)
{
    // The family transport rule (mu-core TransportResolver): a plugin follows the host's play, tempo
    // and beat, and uses its own transport only when the host gives no position at all.
    const auto t = mu_core::resolveTransport(pollHostTransport(), false, midiClockSync, 0.0,
                                             { internalPlaying, internalBpm, internalBeatPos, &nudgePhaseBeats, &nudgeDirection, &appliedSyncOffsetBeats },
                                             juce::jmax(1, numSamples), currentSampleRate);
    noteBlockTransport(t);

    sequencerPlaying.store(t.playing);
    lastBeatPos.store(t.startBeat);
    return { t.playing, t.startBeat };
}

void PluginProcessor::advanceSequencer(bool playing, double beatPos, juce::MidiBuffer& midi, int numSamples)
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
    midiEngine.processBlock(midi, juce::jmax(1, numSamples));
}

//==============================================================================
void PluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream(destData, true).writeString(session::toXmlString(apvts));
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    const auto xml = juce::String::fromUTF8((const char*) data, sizeInBytes);

    // The edit runs under suspendProcessing + rhythmsLock: suspend alone does not stop a running
    // block, the lock does (the audio thread's try-lock bails while it is held).
    suspendProcessing(true);
    juce::String error;
    bool ok = false;
    {
        const juce::ScopedLock sl(rhythmsLock);
        {
            mu_core::ScopedApvtsLoading guard(apvtsLoading);
            ok = session::restoreFromXmlString(xml, apvts, error);
        }
        // JUCE skips listener callbacks for unchanged values, so rebuild the rhythm from the parameters.
        syncRhythmFromApvts();
    }
    suspendProcessing(false);

    if (! ok && onLoadError)
        onLoadError(error);
}

} // namespace mu_clid_midi

// The JUCE plugin factory is the one entry point that stays at global scope.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mu_clid_midi::PluginProcessor();
}
