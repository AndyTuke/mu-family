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

    ampEnv.setSampleRate(sampleRate);
    filterEnv.setSampleRate(sampleRate);
    pitchEnv.setSampleRate(sampleRate);

    filter.prepare({ sampleRate, static_cast<uint32_t>(blockSize), 2 });
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

void VoiceEngine::trigger()
{
    applyPendingParams();

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

    ampEnv.noteOn();
    filterEnv.noteOn();
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

    float semitones = static_cast<float>(activeParams.pitchOctave) * 12.0f
                    + static_cast<float>(activeParams.pitchSemitones)
                    + activeParams.pitchFine / 100.0f;

    float pitchEnvVal = 0.0f;
    for (int i = 0; i < ns; ++i)
        pitchEnvVal = pitchEnv.getNextSample();

    double pitchRatio = playbackRatio
                      * std::pow(2.0, (semitones + pitchEnvVal * activeParams.pitchEnvDepth) / 12.0);

    const int nCh = juce::jmin(output.getNumChannels(), tempBuffer.getNumChannels());
    tempBuffer.clear();

    for (auto& v : voices)
        v.process(buffer, pitchRatio, tempBuffer, ns);

    ampEnv.applyEnvelopeToBuffer(tempBuffer, 0, ns);

    // Filter envelope modulates cutoff (per-block approximation).
    float filterEnvVal = 0.0f;
    for (int i = 0; i < ns; ++i)
        filterEnvVal = filterEnv.getNextSample();

    float modCutoff = activeParams.filterCutoff
                    * std::pow(2.0f, filterEnvVal * activeParams.filterEnvDepth / 12.0f);
    modCutoff = juce::jlimit(20.0f, 20000.0f, modCutoff);
    filter.setCutoffFrequency(modCutoff);

    juce::dsp::AudioBlock<float> block(tempBuffer.getArrayOfWritePointers(),
                                       static_cast<size_t>(nCh),
                                       static_cast<size_t>(0),
                                       static_cast<size_t>(ns));
    juce::dsp::ProcessContextReplacing<float> ctx(block);
    filter.process(ctx);

    for (int ch = 0; ch < nCh; ++ch)
        output.addFrom(ch, 0, tempBuffer, ch, 0, ns, activeParams.ampLevel);
}

bool VoiceEngine::hasSample() const
{
    return sampleLoaded;
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
    using T = juce::dsp::StateVariableTPTFilterType;
    switch (activeParams.filterType)
    {
        case 1:  filter.setType(T::highpass); break;
        case 2:  filter.setType(T::bandpass); break;
        default: filter.setType(T::lowpass);  break;
    }
    filter.setCutoffFrequency(activeParams.filterCutoff);
    filter.setResonance(activeParams.filterRes);
}
