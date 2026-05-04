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
    toneFiltState[0] = toneFiltState[1] = 0.0f;
    prevDriveX[0]   = prevDriveX[1]   = 0.0f;

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
                    + activeParams.pitchFine / 100.0f
                    + activeParams.pitchMod;

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

    // ── Drive stage ──────────────────────────────────────────────────────────
    if (activeParams.driveDrive > 0.01f)
    {
        const float preGain = std::pow(10.0f, activeParams.driveDrive / 100.0f * 2.0f);
        const float outGain = std::pow(10.0f, activeParams.driveOutput / 20.0f);

        // ADAA antiderivatives — guard against cosh overflow for large inputs.
        auto ad1Tanh = [](float x) -> float {
            const float ax = std::abs(x);
            return ax > 12.0f ? ax - 0.6931472f : std::log(std::cosh(x));
        };
        auto ad1Clip = [](float x) -> float {
            if (x >  1.0f) return x - 0.5f;
            if (x < -1.0f) return -x - 0.5f;
            return x * x * 0.5f;
        };

        for (int ch = 0; ch < nCh; ++ch)
        {
            auto* data  = tempBuffer.getWritePointer(ch);
            float xPrev = prevDriveX[ch < 2 ? ch : 0];

            for (int i = 0; i < ns; ++i)
            {
                const float x  = data[i] * preGain;
                const float dx = x - xPrev;
                float y;

                switch (activeParams.driveChar)
                {
                    case 1: // Hard clip — ADAA
                        if (std::abs(dx) < 1e-4f)
                            y = juce::jlimit(-1.0f, 1.0f, 0.5f * (x + xPrev));
                        else
                            y = (ad1Clip(x) - ad1Clip(xPrev)) / dx;
                        break;
                    case 2: // Triangular foldback — direct (ADAA for foldback is complex)
                        {
                            float fx = juce::jlimit(-4.0f, 4.0f, x);
                            while (fx > 1.0f || fx < -1.0f)
                            {
                                if (fx > 1.0f)  fx = 2.0f - fx;
                                if (fx < -1.0f) fx = -2.0f - fx;
                            }
                            y = fx;
                        }
                        break;
                    case 3: // Bit crush — direct
                        {
                            const float bits = juce::jmax(2.0f, 16.0f - activeParams.driveDrive / 100.0f * 12.0f);
                            const float q    = std::pow(2.0f, bits - 1.0f);
                            y = std::round(x * q) / q;
                        }
                        break;
                    default: // Soft (tanh) — ADAA
                        if (std::abs(dx) < 1e-4f)
                            y = std::tanh(0.5f * (x + xPrev));
                        else
                            y = (ad1Tanh(x) - ad1Tanh(xPrev)) / dx;
                        break;
                }

                data[i] = y * outGain;
                xPrev   = x;
            }

            prevDriveX[ch < 2 ? ch : 0] = xPrev;
        }
    }

    // ── Tone filter (first-order IIR LP after drive) ─────────────────────────
    if (activeParams.driveTone < 19000.0f && currentSampleRate > 0.0)
    {
        const float a = std::exp(-juce::MathConstants<float>::twoPi
                                 * activeParams.driveTone / static_cast<float>(currentSampleRate));
        for (int ch = 0; ch < nCh; ++ch)
        {
            auto* data = tempBuffer.getWritePointer(ch);
            float state = toneFiltState[ch];
            for (int i = 0; i < ns; ++i)
            {
                state   = (1.0f - a) * data[i] + a * state;
                data[i] = state;
            }
            toneFiltState[ch] = state;
        }
    }

    for (int ch = 0; ch < nCh; ++ch)
        output.addFrom(ch, 0, tempBuffer, ch, 0, ns, activeParams.ampLevel * accentGain);
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
    filter.setResonance(juce::jmax(0.01f, activeParams.filterRes));
}
