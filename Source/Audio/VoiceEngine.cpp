#include "VoiceEngine.h"
#include <cmath>
#include <limits>

VoiceEngine::VoiceEngine()
{
    formatManager.registerBasicFormats();
    syncEnvelopes();
    syncFilter();
}

void VoiceEngine::prepareToPlay(double sampleRate, int blockSize)
{
    currentSampleRate = sampleRate;

    {
        juce::ScopedWriteLock sl(bufferLock);
        playbackRatio = buffer.getNumSamples() > 0
                      ? originalSampleRate / currentSampleRate
                      : 1.0;
    }

    tempBuffer.setSize(2, blockSize, false, true, false);
    for (auto& vb : voiceBuffers)
        vb.setSize(2, blockSize, false, true, false);
    insertProc.prepare(sampleRate, blockSize);

    for (auto& env : ampEnvs)
        env.setSampleRate(sampleRate);
    filterEnv.setSampleRate(sampleRate);
    pitchEnv.setSampleRate(sampleRate);

    voiceFilter.prepare(sampleRate, blockSize, 2);
    lowCutFilter.prepare(sampleRate, blockSize, 2);
    // Smooth the low-cut frequency so sweeps through the audible range produce
    // a glide rather than a per-block coefficient step.
    smoothedLowCutHz.reset(sampleRate, 0.015);
    smoothedLowCutHz.setCurrentAndTargetValue(activeParams.filterLowCutHz);

    // per-sample pitch ratio buffer + 5 ms ramp on pitchMod.
    // Pre-fill ratios with playbackRatio so SamplePlayer always receives a
    // valid positive ratio even if process() is called before the first
    // setActiveParams() — defensive against any host-init ordering quirk.
    pitchRatioBuffer.assign(static_cast<size_t>(juce::jmax(1, blockSize)), playbackRatio);
    smoothedPitchMod.reset(sampleRate, 0.005);
    smoothedPitchMod.setCurrentAndTargetValue(activeParams.pitchMod);

    syncFilter();
}

void VoiceEngine::loadFile(const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr)
        return;

    if (reader->numChannels == 0 || reader->lengthInSamples <= 0)
        return;
    if (reader->lengthInSamples > static_cast<juce::int64>(std::numeric_limits<int>::max()))
        return;

    const int numSamples  = static_cast<int>(reader->lengthInSamples);
    const int numChannels = static_cast<int>(reader->numChannels);
    juce::AudioBuffer<float> temp(numChannels, numSamples);
    reader->read(&temp, 0, numSamples, 0, true, true);

    const double srcRate = reader->sampleRate;

    {
        juce::ScopedWriteLock sl(bufferLock);
        buffer             = std::move(temp);
        originalSampleRate = srcRate;
        playbackRatio      = srcRate / currentSampleRate;
        sampleLoaded       = true;
    }
}

void VoiceEngine::trigger(bool isAccented, bool tied)
{
    applyPendingParams();

    accentGain = isAccented ? juce::Decibels::decibelsToGain(activeParams.accentDb) : 1.0f;

    // tied (pattern-legato) triggers ask SamplePlayer for a short linear
    // fade-in so the sample-voice restart against a running envelope tail
    // doesn't click. 64 samples ≈ 1.3 ms @ 48 kHz — inaudible as a fade, long
    // enough to bridge the sample[0] discontinuity. Untied triggers get 0
    // (transient-preserving for drum hits — the default behaviour).
    static constexpr int kTiedFadeInSamples = 64;
    const int fadeIn = tied ? kTiedFadeInSamples : 0;

    // Claim a voice slot. In poly mode (default): first inactive slot wins;
    // if none, round-robin steal the oldest. In mono mode (voiceMono): every
    // hit forces slot 0 — gives classic mono-synth retrigger behaviour where
    // the previous voice is cut by the new attack.
    int claimedIdx = 0;
    if (activeParams.voiceMono)
    {
        voices[0].trigger(fadeIn);
    }
    else
    {
        claimedIdx = -1;
        for (int i = 0; i < MaxVoices; ++i)
        {
            if (!voices[(size_t) i].isActive()) { voices[(size_t) i].trigger(fadeIn); claimedIdx = i; break; }
        }
        if (claimedIdx < 0)
        {
            claimedIdx = nextVoice;
            voices[(size_t) claimedIdx].trigger(fadeIn);
            nextVoice = (nextVoice + 1) % MaxVoices;
        }
    }

    // tied hits skip the envelope retrigger entirely — env state carries
    // through across contiguous pattern hits, so a long decay can finally
    // breathe past the step length. The per-envelope legato flags still
    // apply on UNtied hits (i.e. when the user transitions from rest → hit, or
    // when patternLegato is disabled at the sequencer level).
    if (tied)
        return;

    // Per-voice amp envelope retrigger on the claimed slot only — slots still
    // releasing from earlier hits keep their envelope state intact and play
    // out independently. Reset (default) clears to zero before noteOn; Legato
    // continues from the current level so rapid retriggers on the same slot
    // don't click on pad/melodic material.
    auto& ampEnv = ampEnvs[(size_t) claimedIdx];
    if (!activeParams.ampEnvLegato) ampEnv.reset();
    ampEnv.noteOn();

    // Filter / pitch envelopes are still monophonic (one envelope per engine,
    // applied to the mixed signal / pitch ratio). Global retrigger as before.
    if (!activeParams.filterEnvLegato) filterEnv.reset();
    filterEnv.noteOn();
    if (!activeParams.pitchEnvLegato)  pitchEnv.reset();
    pitchEnv.noteOn();
}

void VoiceEngine::process(juce::AudioBuffer<float>& output, int numSamples)
{
    applyPendingParams();

    juce::ScopedReadLock sl(bufferLock);
    if (!sampleLoaded || buffer.getNumSamples() == 0 || buffer.getNumChannels() == 0)
        return;

    // Guard against hosts that call process() with more samples than prepareToPlay() promised.
    const int ns = juce::jmin(numSamples, tempBuffer.getNumSamples());

    // per-sample pitch ratio. Static pitch and base playback ratio fold
    // into a constant; pitch envelope (sample-accurate read) + smoothed pitchMod
    // produce the per-sample exponent.
    // the prior "defensive grow" `if (size < ns) assign(ns, ...)` was dead
    // code (audio-thread alloc that could never fire): `ns` is already clamped to
    // `tempBuffer.getNumSamples()` at the top of process() — which equals the
    // blockSize that prepareToPlay assigned to BOTH tempBuffer AND pitchRatioBuffer.
    // The two stay in lock-step because both are sized from the same blockSize
    // argument. Removing the dead assign() also removes a Tier 2 audio-thread
    // allocation hazard that would have fired if the surrounding clamp ever
    // regressed.
    const float baseSemitones = static_cast<float>(activeParams.pitchOctave) * 12.0f
                              + static_cast<float>(activeParams.pitchSemitones)
                              + activeParams.pitchFine / 100.0f;
    const float pitchDepth = activeParams.pitchEnvDepth;

    smoothedPitchMod.setTargetValue(activeParams.pitchMod);

    for (int s = 0; s < ns; ++s)
    {
        const float envVal   = pitchEnv.getNextSample();
        const float modVal   = smoothedPitchMod.getNextValue();
        const float semitone = baseSemitones + modVal + envVal * pitchDepth;
        pitchRatioBuffer[static_cast<size_t>(s)] = playbackRatio * std::pow(2.0, semitone / 12.0);
    }

    const int nCh = juce::jmin(output.getNumChannels(), tempBuffer.getNumChannels());
    tempBuffer.clear();

    // Per-voice render + envelope. Each sample slot renders into its own
    // scratch buffer so its amp envelope (`ampEnvs[i]`) applies in isolation —
    // a long release on slot 0 plays out untouched even when slot 1 attacks
    // a new hit. Skip slots that are fully idle (no in-flight sample, no
    // envelope tail) — saves the per-slot env-apply on unused voices. We
    // always apply the envelope when active, regardless of ampRelToEnd:
    // ampRelToEnd's intent is "let the sample play through to its natural end
    // instead of being cut by the RELEASE phase," not "bypass the envelope
    // completely." During normal play (no noteOff, env held in Sustain),
    // applying the env only affects A/D/S. The release-to-end semantic is
    // enforced in markRetired() instead — for ampRelToEnd, we skip the
    // noteOff so the env stays at sustain level.
    for (int vi = 0; vi < MaxVoices; ++vi)
    {
        if (! voices[(size_t) vi].isActive() && ! ampEnvs[(size_t) vi].isActive())
            continue;
        auto& vb = voiceBuffers[(size_t) vi];
        vb.clear();
        voices[(size_t) vi].process(buffer, pitchRatioBuffer.data(), vb, ns);
        ampEnvs[(size_t) vi].applyEnvelopeToBuffer(vb, 0, ns);
        for (int ch = 0; ch < nCh; ++ch)
            tempBuffer.addFrom(ch, 0, vb, ch, 0, ns);
    }

    // Filter envelope modulates cutoff (per-block approximation).
    float filterEnvVal = 0.0f;
    for (int i = 0; i < ns; ++i)
        filterEnvVal = filterEnv.getNextSample();

    float modCutoff = activeParams.filterCutoff
                    * std::pow(2.0f, filterEnvVal * activeParams.filterEnvDepth / 12.0f);
    modCutoff = juce::jlimit(20.0f, 20000.0f, modCutoff);

    voiceFilter.setCutoff(modCutoff);
    voiceFilter.process(tempBuffer, ns, nCh);

    // 4-pole high-pass cleanup after the main filter. Bypassed (no DSP work)
    // when the user knob is at 0; otherwise smoothed toward the target so a
    // sweep glides instead of stepping per block.
    smoothedLowCutHz.setTargetValue(activeParams.filterLowCutHz);
    const float lowCutHz = smoothedLowCutHz.skip(ns);
    if (lowCutHz > 0.5f)
        lowCutFilter.process(tempBuffer, ns, nCh, lowCutHz, 0.0f);

    // ── Insert effects (delegated to InsertProcessor) ────────────────────────
    insertProc.process(tempBuffer, ns, nCh, activeParams);

    for (int ch = 0; ch < nCh; ++ch)
        output.addFrom(ch, 0, tempBuffer, ch, 0, ns, activeParams.ampLevel * accentGain);
}

bool VoiceEngine::hasSample() const
{
    return sampleLoaded;
}

void VoiceEngine::markRetired() noexcept
{
    // Caller (handleAsyncUpdate retire path) holds suspendProcessing — the
    // audio thread cannot be mid-process() on this engine, so direct state
    // mutation is safe.

    // Per-voice amp envelope noteOff is gated on ampRelToEnd.
    //  - ampRelToEnd == false: noteOff every voice so each env enters release;
    //    applyEnvelopeToBuffer (always called when the slot is active, see
    //    process()) fades each slot over the user's release time, then every
    //    env reaches idle and the engine drains.
    //  - ampRelToEnd == true: do NOT noteOff. Envs stay at whatever level
    //    they reached and applyEnvelopeToBuffer keeps multiplying by that —
    //    samples play through to their natural ends at the sustain-decided
    //    volume. isFullyDrained() handles the voices-only drain criterion.
    if (!activeParams.ampRelToEnd)
        for (auto& env : ampEnvs)
            env.noteOff();

    // Filter / pitch envelopes don't have an ampRelToEnd-equivalent — they
    // always have a finite release. noteOff so they progress to idle; the
    // filter is reset below anyway so filterEnv's modulation has no audible
    // effect, but ticking it to idle keeps internal state tidy.
    filterEnv.noteOff();
    pitchEnv.noteOff();

    voiceFilter.reset();
    lowCutFilter.reset();
    insertProc.reset();
    retired = true;
}

bool VoiceEngine::isFullyDrained() const noexcept
{
    // SamplePlayer::isActive() == (playPos >= 0 || triggered) — a "pending trigger
    // that hasn't been picked up yet" still counts as not-drained so we never
    // destroy an engine that's about to start rendering on its next process().
    for (const auto& v : voices)
        if (v.isActive())
            return false;

    // ampRelToEnd retired engines never get noteOff'd (per-voice envs stay at
    // sustain level, sample plays through unscaled). isActive() therefore
    // never returns false for them. Voices-finished is the sole drain
    // criterion in that mode — at which point the engine produces silence
    // regardless of where the envs are.
    if (retired && activeParams.ampRelToEnd)
        return true;

    // Standard mode: every per-voice envelope must be idle (post-release) for
    // the engine to be drainable. Any one still active means it's still
    // multiplying a (potentially silent) sample by something non-zero — but
    // since the voices loop above already ensured no sample is rendering,
    // the env tail produces silence and we COULD drain. However, JUCE ADSR
    // can hit isActive()==true with non-zero internal level after a noteOff,
    // and we don't want to destroy the engine while any env is mid-release
    // since it indicates "still wants to be processed." Keep the original
    // semantics: every per-voice env must report idle.
    for (const auto& env : ampEnvs)
        if (env.isActive())
            return false;

    return true;
}

void VoiceEngine::clearSample()
{
    juce::ScopedWriteLock sl(bufferLock);
    buffer.setSize(0, 0);
    sampleLoaded = false;
}

void VoiceEngine::setParams(const VoiceParams& p)
{
    {
        juce::SpinLock::ScopedLockType sl(pendingLock);
        pendingParams = p;
    }
    paramsDirty.store(true, std::memory_order_release);
}

void VoiceEngine::setActiveParams(const VoiceParams& p)
{
    // Audio thread only — overrides activeParams with modulated values for this block.
    activeParams = p;
    syncEnvelopes();
    syncFilter();
}

void VoiceEngine::applyPendingParams()
{
    if (!paramsDirty.exchange(false, std::memory_order_acquire))
        return;
    {
        juce::SpinLock::ScopedLockType sl(pendingLock);
        params = pendingParams;
    }
    // Reset activeParams to base params — modulation reapplies each block.
    activeParams = params;
    syncEnvelopes();
    syncFilter();
}

void VoiceEngine::syncEnvelopes()
{
    // Attack is allowed to be exactly 0 — juce::ADSR detects the zero case and
    // jumps the envelope straight to peak on noteOn (no ramp). Decay and
    // release keep a tiny floor because zero there would freeze the envelope
    // in those phases (division-by-zero on the per-sample decrement rate).
    // Sustain is amplitude, not time, so 0 is always legal there.
    constexpr float kMinDecayRelSec = 0.001f;
    const juce::ADSR::Parameters ampParams { activeParams.ampEnvAtk,
                                             juce::jmax(kMinDecayRelSec, activeParams.ampEnvDec),
                                             activeParams.ampEnvSus,
                                             juce::jmax(kMinDecayRelSec, activeParams.ampEnvRel) };
    for (auto& env : ampEnvs)
        env.setParameters(ampParams);
    filterEnv.setParameters({ activeParams.filterEnvAtk,
                              juce::jmax(kMinDecayRelSec, activeParams.filterEnvDec),
                              activeParams.filterEnvSus,
                              juce::jmax(kMinDecayRelSec, activeParams.filterEnvRel) });
    pitchEnv.setParameters ({ activeParams.pitchEnvAtk,
                              juce::jmax(kMinDecayRelSec, activeParams.pitchEnvDec),
                              activeParams.pitchEnvSus,
                              juce::jmax(kMinDecayRelSec, activeParams.pitchEnvRel) });
}

void VoiceEngine::syncFilter()
{
    voiceFilter.setType(activeParams.filterType);
    voiceFilter.setCutoff(activeParams.filterCutoff);
    voiceFilter.setResonance(activeParams.filterRes);
}
