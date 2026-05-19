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
    insertProc.prepare(sampleRate, blockSize);

    ampEnv.setSampleRate(sampleRate);
    filterEnv.setSampleRate(sampleRate);
    pitchEnv.setSampleRate(sampleRate);

    voiceFilter.prepare(sampleRate, blockSize, 2);

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

    bool claimed = false;
    for (auto& v : voices)
    {
        if (!v.isActive()) { v.trigger(fadeIn); claimed = true; break; }
    }
    if (!claimed)
    {
        voices[nextVoice].trigger(fadeIn);
        nextVoice = (nextVoice + 1) % MaxVoices;
    }

    // tied hits skip the envelope retrigger entirely — env state carries
    // through across contiguous pattern hits, so a long decay can finally
    // breathe past the step length. The per-envelope #221 legato flags still
    // apply on UNtied hits (i.e. when the user transitions from rest → hit, or
    // when patternLegato is disabled at the sequencer level).
    if (tied)
        return;

    // Reset (default) clears envelope to zero before noteOn; Legato continues
    // from the current level so rapid retriggers don't click on pad/melodic material.
    if (!activeParams.ampEnvLegato)    ampEnv.reset();
    ampEnv.noteOn();
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

    for (auto& v : voices)
        v.process(buffer, pitchRatioBuffer.data(), tempBuffer, ns);

    // always apply the amp envelope, regardless of ampRelToEnd. The prior
    // gate skipped applyEnvelopeToBuffer entirely when ampRelToEnd was true,
    // which made the user's sustain=0 + decay=4s setting produce no decay at all
    // — the sample played at full level forever. ampRelToEnd's intent is "let
    // the sample play through to its natural end instead of being cut by the
    // RELEASE phase," not "bypass the envelope completely." Since this codebase
    // never calls noteOff during normal play (drum-style triggers — env holds
    // in Sustain), applying the env here only affects A/D/S. The release-to-
    // end semantic is enforced in markRetired() instead: for ampRelToEnd, we
    // skip the noteOff so the env stays at sustain level and the sample plays
    // through unscaled (instead of fading via the release time).
    ampEnv.applyEnvelopeToBuffer(tempBuffer, 0, ns);

    // Filter envelope modulates cutoff (per-block approximation).
    float filterEnvVal = 0.0f;
    for (int i = 0; i < ns; ++i)
        filterEnvVal = filterEnv.getNextSample();

    float modCutoff = activeParams.filterCutoff
                    * std::pow(2.0f, filterEnvVal * activeParams.filterEnvDepth / 12.0f);
    modCutoff = juce::jlimit(20.0f, 20000.0f, modCutoff);

    voiceFilter.setCutoff(modCutoff);
    voiceFilter.process(tempBuffer, ns, nCh);

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

    // amp envelope noteOff is gated on ampRelToEnd.
    //  - ampRelToEnd == false: noteOff so the env enters release; applyEnvelope-
    //    ToBuffer (always called now, see process()) fades the sample over the
    //    user's release time, then env reaches idle and the engine drains.
    //  - ampRelToEnd == true: do NOT noteOff. Env stays at sustain level (or
    //    wherever decay left it) and applyEnvelopeToBuffer keeps multiplying
    //    by that level — sample plays through to its natural end at the
    //    sustain-decided volume. isFullyDrained() handles the voices-only
    //    drain criterion for this case.
    if (!activeParams.ampRelToEnd)
        ampEnv.noteOff();

    // Filter / pitch envelopes don't have an ampRelToEnd-equivalent — they
    // always have a finite release. noteOff so they progress to idle; the
    // filter is reset below anyway so filterEnv's modulation has no audible
    // effect, but ticking it to idle keeps internal state tidy.
    filterEnv.noteOff();
    pitchEnv.noteOff();

    voiceFilter.reset();
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

    // ampRelToEnd retired engines never get noteOff'd (env stays at
    // sustain level, sample plays through unscaled). isActive() therefore
    // never returns false for them. Voices-finished is the sole drain
    // criterion in that mode — at which point the engine produces silence
    // regardless of where the env is.
    if (retired && activeParams.ampRelToEnd)
        return true;

    // Standard mode: env idle (post-release) means the engine multiplies its
    // sample by 0 → silent output, safe to destroy.
    if (ampEnv.isActive())
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
    // apply the 1 ms floor at the JUCE ADSR boundary instead of in
    // applyRhythmSuffix's data layer. JUCE's ADSR doesn't accept zero times,
    // but storing the floor inside voiceParams polluted UI reads — a knob
    // set to 0 round-tripped as 0.001 and visibly crept up on the next
    // refreshSuffix(). Floor is per-time-param only; sustains are amplitude
    // (0 is legal — that's the user's "silence after decay" intent).
    constexpr float kMinAdsrSec = 0.001f;
    ampEnv.setParameters   ({ juce::jmax(kMinAdsrSec, activeParams.ampEnvAtk),
                              juce::jmax(kMinAdsrSec, activeParams.ampEnvDec),
                              activeParams.ampEnvSus,
                              juce::jmax(kMinAdsrSec, activeParams.ampEnvRel) });
    filterEnv.setParameters({ juce::jmax(kMinAdsrSec, activeParams.filterEnvAtk),
                              juce::jmax(kMinAdsrSec, activeParams.filterEnvDec),
                              activeParams.filterEnvSus,
                              juce::jmax(kMinAdsrSec, activeParams.filterEnvRel) });
    pitchEnv.setParameters ({ juce::jmax(kMinAdsrSec, activeParams.pitchEnvAtk),
                              juce::jmax(kMinAdsrSec, activeParams.pitchEnvDec),
                              activeParams.pitchEnvSus,
                              juce::jmax(kMinAdsrSec, activeParams.pitchEnvRel) });
}

void VoiceEngine::syncFilter()
{
    voiceFilter.setType(activeParams.filterType);
    voiceFilter.setCutoff(activeParams.filterCutoff);
    voiceFilter.setResonance(activeParams.filterRes);
}
