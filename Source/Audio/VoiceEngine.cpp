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

    // #220 / #219: per-sample pitch ratio buffer + 5 ms ramp on pitchMod.
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

void VoiceEngine::trigger(bool isAccented)
{
    applyPendingParams();

    accentGain = isAccented ? juce::Decibels::decibelsToGain(activeParams.accentDb) : 1.0f;

    bool claimed = false;
    for (auto& v : voices)
    {
        if (!v.isActive()) { v.trigger(); claimed = true; break; }
    }
    if (!claimed)
    {
        voices[nextVoice].trigger();
        nextVoice = (nextVoice + 1) % MaxVoices;
    }

    // #221: Reset (default) clears envelope to zero before noteOn; Legato continues
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

    // #220 / #219: per-sample pitch ratio. Static pitch and base playback ratio fold
    // into a constant; pitch envelope (sample-accurate read) + smoothed pitchMod
    // produce the per-sample exponent. Defensive: if pitchRatioBuffer is somehow
    // smaller than ns (host called process before prepareToPlay matched the block
    // size — rare but possible), grow it via assign() filled with playbackRatio so
    // SamplePlayer always sees a valid positive ratio.
    const float baseSemitones = static_cast<float>(activeParams.pitchOctave) * 12.0f
                              + static_cast<float>(activeParams.pitchSemitones)
                              + activeParams.pitchFine / 100.0f;
    const float pitchDepth = activeParams.pitchEnvDepth;

    smoothedPitchMod.setTargetValue(activeParams.pitchMod);

    if (static_cast<int>(pitchRatioBuffer.size()) < ns)
        pitchRatioBuffer.assign(static_cast<size_t>(ns), playbackRatio);

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

    if (!activeParams.ampRelToEnd)
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
    ampEnv.setParameters   ({ activeParams.ampEnvAtk,    activeParams.ampEnvDec,
                              activeParams.ampEnvSus,    activeParams.ampEnvRel    });
    filterEnv.setParameters({ activeParams.filterEnvAtk, activeParams.filterEnvDec,
                              activeParams.filterEnvSus, activeParams.filterEnvRel });
    pitchEnv.setParameters ({ activeParams.pitchEnvAtk,  activeParams.pitchEnvDec,
                              activeParams.pitchEnvSus,  activeParams.pitchEnvRel  });
}

void VoiceEngine::syncFilter()
{
    voiceFilter.setType(activeParams.filterType);
    voiceFilter.setCutoff(activeParams.filterCutoff);
    voiceFilter.setResonance(activeParams.filterRes);
}
