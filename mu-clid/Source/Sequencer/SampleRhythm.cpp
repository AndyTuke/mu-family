// SampleRhythm — the per-block hit and play state (audio thread). Moved from the processor's
// advanceSequencer (E5.4b); the processor still runs the sequencer and calls these per rhythm.

#include "Sequencer/SampleRhythm.h"
#include "Audio/MidiOutputMapping.h"   // mu-core: velocity for the MIDI output mode

namespace mu_clid {

void SampleRhythm::fireHit(bool accented, bool tied, juce::MidiBuffer& midiOut, MidiOutputEngine& midiEngine,
                           VoiceEngine* engine)
{
    // A hit in MIDI output mode becomes one MIDI note-on and never reaches the voice. The settings are
    // relaxed atomics on the Rhythm.
    if (outputMode.load() == (int) mu_core::OutputMode::Midi)
    {
        midiEngine.trigger(midiOut, 0, midiNote.load(), midiChannel.load(),
                     mu_core::midiOutVelocity(midiVelocity.load(), (float) midiAccent.load(), accented));
    }
    else if (engine != nullptr)
    {
        // Pattern-legato gating: the sequencer always marks tied hits; the rhythm's patternLegato flag
        // decides whether to act on it. Untied / legato-off hits go through the standard retrigger.
        engine->trigger(accented, patternLegato && tied);
    }
}

void SampleRhythm::publishPlayState(RhythmPlayState& state, int step, int accentStep, int patternLength,
                                    float frac, bool fired) const noexcept
{
    // Step and fraction go out as one snapshot so the ring never pairs a new fraction with an old step.
    state.publishPosition({ step, accentStep, frac });
    state.patternLength.store(patternLength);
    state.stepsA.store(juce::jmax(1, genA.steps));
    state.stepsB.store(juce::jmax(1, genB.steps));
    state.stepsC.store(juce::jmax(1, genC.steps));
    if (fired)
        state.hitCount.store(state.hitCount.load() + 1);   // monotonic counter for race-free UI hit detection
}

} // namespace mu_clid
