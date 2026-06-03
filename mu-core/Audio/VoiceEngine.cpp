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

    // Engine-level amp envelope (single ADSR for the whole engine, not per-voice).
    // Untied retrigger resets to zero and noteOns; the voice slot's sample audio
    // mixes into tempBuffer and the env multiplies the post-filter signal before
    // the insert. Tied hits already returned above so env state carries across
    // contiguous pattern hits (pattern legato unchanged).
    (void) claimedIdx;  // slot index no longer needed for env routing
    ampEnv.reset();
    ampEnv.noteOn();

    // Filter / pitch envelopes — also engine-level, also retrigger on untied hits.
    filterEnv.reset();
    filterEnv.noteOn();
    pitchEnv.reset();
    pitchEnv.noteOn();
}

void VoiceEngine::process(juce::AudioBuffer<float>& output, int numSamples)
{
    applyPendingParams();

    // tryEnterRead avoids blocking the audio thread if loadFile() holds the
    // write lock (file I/O + buffer allocation can be hundreds of ms). On
    // contention: output a silent block rather than stalling the audio thread.
    if (!bufferLock.tryEnterRead())
        return;
    struct ReadGuard { juce::ReadWriteLock& l; ~ReadGuard() { l.exitRead(); } } readGuard { bufferLock };

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
    // Static pitch shift clamped to ±4 octaves (octave ±3 oct = ±36 semis + semi ±12 + fine ±1
    // could otherwise reach ±49 semis; Andy's spec: accept clamping when fine adds beyond ±4 oct).
    const float baseSemitones = juce::jlimit(-48.0f, 48.0f,
                                  static_cast<float>(activeParams.pitchOctave) * 12.0f
                                + static_cast<float>(activeParams.pitchSemitones)
                                + activeParams.pitchFine / 100.0f);
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

    // Signal chain: sample → filter → ampEnv → insert → output.
    // The ampEnv sits BETWEEN filter and insert, so:
    //   • Filter resonance / comb feedback in the filter stage is env-gated — it
    //     rings during env release and is silenced at env idle.
    //   • Insert-stage FX (Karplus, Vocoder feedback paths, reverb-style) sit
    //     AFTER the env gate, so they receive env-shaped input but their own
    //     internal state (delay-line feedback, etc.) decays at the FX's own
    //     natural rate — Karplus tails can extend past env idle.
    //   • Retired engines' insert tails are bounded: Karplus loopGain is capped
    //     at 0.9999 (decays in ~5 min worst case, sub-second typical). The
    //     `retiredDrainBlocks` counter in markRetired delays isFullyDrained by
    //     ~2 s past env-idle so audible feedback FX tails aren't cut by engine
    //     destruction during hot-swap (the env-gate at filter output keeps the
    //     retired engine's filter resonance from leaking into the next active rhythm).
    for (int vi = 0; vi < MaxVoices; ++vi)
    {
        if (! voices[(size_t) vi].isActive())
            continue;
        voices[(size_t) vi].process(buffer, pitchRatioBuffer.data(), tempBuffer, ns);
    }

    // Filter envelope modulates cutoff — per-block approximation. JUCE ADSR has
    // no skip() so all ns samples are advanced to keep envelope state correct,
    // but only the final sample is used to set the cutoff for the block. This is
    // intentional: the filter biquad coefficients change at most once per block
    // anyway (setCutoff is called once below), so sample-accurate cutoff modulation
    // would require a per-sample filter update loop that is not worth the cost
    // for a slowly-evolving envelope-driven filter.
    float filterEnvVal = 0.0f;
    for (int i = 0; i < ns; ++i)
        filterEnvVal = filterEnv.getNextSample();

    float modCutoff = activeParams.filterCutoff
                    * std::pow(2.0f, filterEnvVal * activeParams.filterEnvDepth / 12.0f);
    modCutoff = juce::jlimit(20.0f, 20000.0f, modCutoff);

    voiceFilter.setDrive(activeParams.filterDrive);
    voiceFilter.setLowCut(activeParams.filterLowCutHz);
    voiceFilter.setCutoff(modCutoff);
    voiceFilter.process(tempBuffer, ns, nCh);

    // Apply ampEnv to the post-filter signal BEFORE insert. Filter ringing
    // is gated; insert receives env-shaped input but feedback-based inserts
    // (Karplus, etc.) ring on past env idle from their own internal state.
    ampEnv.applyEnvelopeToBuffer(tempBuffer, 0, ns);

    // ── Insert effects (delegated to InsertProcessor) ────────────────────────
    insertProc.process(tempBuffer, ns, nCh, activeParams);

    if (retired && retireDrainBlocks > 0)
        --retireDrainBlocks;

    // ampLevel is dB-domain (-60..+6); convert to linear gain once per block.
    const float levelGain = juce::Decibels::decibelsToGain(activeParams.ampLevel, -60.0f);
    for (int ch = 0; ch < nCh; ++ch)
        output.addFrom(ch, 0, tempBuffer, ch, 0, ns, levelGain * accentGain);
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

    // Amp env noteOff is gated on ampRelToEnd:
    //  - ampRelToEnd == false: noteOff so the env enters release. ampEnv multiplies
    //    the post-filter signal during release, so filter resonance fades into the
    //    env's release curve. Insert state survives — its own feedback decay continues
    //    past env idle, drained on the `retireDrainBlocks` timer.
    //  - ampRelToEnd == true: do NOT noteOff. Env stays at sustain (or wherever decay
    //    left it); sample plays through to its natural end at that level.
    //    isFullyDrained() switches to a voices-only drain criterion in this mode.
    if (!activeParams.ampRelToEnd)
        ampEnv.noteOff();

    // Filter / pitch envelopes — also engine-level — get a finite release. noteOff
    // so they progress to idle. Filter / insert state is NOT reset on note-off:
    // their tails ring naturally and the ampEnv release gates the output to silence
    // when env hits idle (restores FX-tail behaviour for filter resonance, comb
    // feedback, and reverb-style insert tails).
    filterEnv.noteOff();
    pitchEnv.noteOff();

    // ~2 s drain budget for feedback-based inserts (Karplus loopGain cap 0.9999
    // decays to noise floor in well under this). Sampled in blocks: 2 s × sampleRate
    // / blockSize. tempBuffer.getNumSamples() == blockSize after prepareToPlay.
    const int blockSize = juce::jmax(1, tempBuffer.getNumSamples());
    retireDrainBlocks = (int) (2.0 * currentSampleRate / (double) blockSize);

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

    // ampRelToEnd retired engines never get noteOff'd (env stays at sustain level,
    // sample plays through unscaled). ampEnv.isActive() therefore never returns
    // false for them. Voices-finished is the sole drain criterion in that mode —
    // at which point applyEnvelopeToBuffer's gain × 0 outputs silence regardless.
    if (retired && activeParams.ampRelToEnd)
        return true;

    // Standard mode: env must be idle AND the post-env-idle drain budget exhausted.
    // Insert is now AFTER ampEnv in the chain (T12), so feedback FX (Karplus) keep
    // ringing past env idle from their own internal state. Wait the drain budget
    // so the audible tail completes before the engine is destroyed in hot-swap.
    if (ampEnv.isActive()) return false;
    if (retired && retireDrainBlocks > 0) return false;
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
    ampEnv.setParameters(ampParams);
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
