// SampleRhythm — the per-block hit, play state and render (audio thread). The hit and play state moved from the
// processor's sequencer pass (E5.4b); the render (voice, retired engines) from the mixer's channel loop (E5.5d).

#include "Sequencer/SampleRhythm.h"
#include "Audio/MidiOutputMapping.h"   // mu-core: velocity for the MIDI output mode

namespace mu_clid {

void SampleRhythm::prepare(double sampleRate, int blockSize)
{
    // Message or host thread, not processing: every engine the layer holds, for this rate and block size.
    if (voice != nullptr)
        voice->prepareToPlay(sampleRate, blockSize);
    midi.prepare(sampleRate, blockSize);
    for (auto& engine : retiredEngines)
        if (engine != nullptr)
            engine->prepareToPlay(sampleRate, blockSize);
}

void SampleRhythm::takeHit(bool accented, bool tied, juce::MidiBuffer& midiOut)
{
    // A hit in MIDI output mode becomes one MIDI note-on now and never reaches the voice; a sample hit waits for
    // render. The output mode is read once, here; the settings are relaxed atomics on the Rhythm.
    if (outputMode.load() == (int) mu_core::OutputMode::Midi)
    {
        midi.trigger(midiOut, 0, midiNote.load(), midiChannel.load(),
                     mu_core::midiOutVelocity(midiVelocity.load(), (float) midiAccent.load(), accented));
        return;
    }
    block.voiceHit = true;
    block.accented = accented;
    block.tied     = tied;
}

void SampleRhythm::renderVoices(juce::AudioBuffer<float>& out, int n)
{
    if (voice != nullptr)
        voice->process(out, n);

    // The retired engines ride the same channel after the voice; one that has drained is flagged for the
    // message thread, which frees it (it keeps rendering until then).
    for (int i = 0; i < kMaxRetiredEngines; ++i)
        if (auto& engine = retiredEngines[(size_t) i])
        {
            engine->process(out, n);
            if (engine->isFullyDrained())
                retiredReadyForCleanup[(size_t) i].store(true, std::memory_order_release);
        }
}

void SampleRhythm::render(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>& out, int n)
{
    // Phase 1, the block's first call: the hit retriggers the voice, then the modulation pass sets the block's
    // voice and strip values (both at the block start, as the processor chain did).
    if (ctx.frameOffset == 0)
    {
        // Pattern-legato gating: the cursor always marks tied hits; the rhythm's patternLegato flag decides whether
        // to act on it. Untied / legato-off hits go through the standard retrigger.
        if (block.voiceHit && voice != nullptr)
            voice->trigger(block.accented, patternLegato && block.tied);
        block.voiceHit = false;
        if (block.strip != nullptr)
            applyModulation(ctx.startBeat, *block.strip, block.lastEffectiveStep, voice.get());
        block.strip = nullptr;
    }

    // Phase 2, every call: the voice and the retired engines into this call's frames.
    renderVoices(out, n);
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
