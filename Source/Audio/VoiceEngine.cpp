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
    toneFilter[0].reset();  toneFilter[1].reset();
    bitAaFilter[0].reset(); bitAaFilter[1].reset();
    prevDriveX[0]    = prevDriveX[1]    = 0.0f;
    bitRateCounter[0] = bitRateCounter[1] = 0.0f;
    bitRateHeld[0]    = bitRateHeld[1]    = 0.0f;

    ampEnv.setSampleRate(sampleRate);
    filterEnv.setSampleRate(sampleRate);
    pitchEnv.setSampleRate(sampleRate);

    filter.prepare({ sampleRate, static_cast<uint32_t>(blockSize), 2 });
    syncFilter();

    const juce::dsp::ProcessSpec spec { sampleRate, static_cast<uint32_t>(blockSize), 2 };
    eqLow.prepare(spec);
    eqMid.prepare(spec);
    eqHigh.prepare(spec);
    compEnvelope[0] = compEnvelope[1] = 0.0f;
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

    ampEnv.reset();    ampEnv.noteOn();
    filterEnv.reset(); filterEnv.noteOn();
    pitchEnv.reset();  pitchEnv.noteOn();
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

    if (!activeParams.ampRelToEnd)
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

    // ── Insert effects (self-contained per algorithm) ────────────────────────
    switch (activeParams.driveChar)
    {
        case 1: // ── Soft clip — tanh ADAA ────────────────────────────────────
        {
            const float preGain = std::pow(10.0f, activeParams.driveDrive / 100.0f * 2.0f);
            const float outGain = std::pow(10.0f, activeParams.driveOutput / 20.0f) / preGain;
            auto ad1Tanh = [](float x) -> float {
                return std::abs(x) > 12.0f ? std::abs(x) - 0.6931472f : std::log(std::cosh(x));
            };
            for (int ch = 0; ch < nCh; ++ch)
            {
                auto*  data  = tempBuffer.getWritePointer(ch);
                float  xPrev = prevDriveX[ch < 2 ? ch : 0];
                for (int i = 0; i < ns; ++i)
                {
                    const float x  = data[i] * preGain;
                    const float dx = x - xPrev;
                    float y = std::abs(dx) < 1e-4f ? std::tanh(0.5f * (x + xPrev))
                                                   : (ad1Tanh(x) - ad1Tanh(xPrev)) / dx;
                    data[i] = y * outGain;
                    xPrev   = x;
                }
                prevDriveX[ch < 2 ? ch : 0] = xPrev;
            }
            break;
        }
        case 2: // ── Hard clip — ADAA ─────────────────────────────────────────
        {
            const float preGain = std::pow(10.0f, activeParams.driveDrive / 100.0f * 2.0f);
            const float outGain = std::pow(10.0f, activeParams.driveOutput / 20.0f) / preGain;
            auto ad1Clip = [](float x) -> float {
                if (x >  1.0f) return x - 0.5f;
                if (x < -1.0f) return -x - 0.5f;
                return x * x * 0.5f;
            };
            for (int ch = 0; ch < nCh; ++ch)
            {
                auto*  data  = tempBuffer.getWritePointer(ch);
                float  xPrev = prevDriveX[ch < 2 ? ch : 0];
                for (int i = 0; i < ns; ++i)
                {
                    const float x  = data[i] * preGain;
                    const float dx = x - xPrev;
                    float y = std::abs(dx) < 1e-4f
                                  ? juce::jlimit(-1.0f, 1.0f, 0.5f * (x + xPrev))
                                  : (ad1Clip(x) - ad1Clip(xPrev)) / dx;
                    data[i] = y * outGain;
                    xPrev   = x;
                }
                prevDriveX[ch < 2 ? ch : 0] = xPrev;
            }
            break;
        }
        case 3: // ── Triangular foldback ──────────────────────────────────────
        {
            const float preGain = std::pow(10.0f, activeParams.driveDrive / 100.0f * 2.0f);
            const float outGain = std::pow(10.0f, activeParams.driveOutput / 20.0f) / preGain;
            for (int ch = 0; ch < nCh; ++ch)
            {
                auto* data = tempBuffer.getWritePointer(ch);
                for (int i = 0; i < ns; ++i)
                {
                    float fx = juce::jlimit(-4.0f, 4.0f, data[i] * preGain);
                    while (fx > 1.0f || fx < -1.0f)
                    {
                        if (fx >  1.0f) fx =  2.0f - fx;
                        if (fx < -1.0f) fx = -2.0f - fx;
                    }
                    data[i] = fx * outGain;
                }
            }
            break;
        }
        case 4: // ── Bitcrusher: bit depth + sample rate + TPDF dither ────────
        {
            const float bits    = juce::jlimit(1.0f, 16.0f, activeParams.drvBits);
            const float q       = std::pow(2.0f, bits - 1.0f);
            const float ratioF  = juce::jmax(1.0f,
                (float)(currentSampleRate / (double)juce::jmax(100.0f, activeParams.driveRate)));
            const float dither  = activeParams.drvDither / 100.0f * (0.5f / q);
            const float aaCut   = juce::jmin(activeParams.driveRate * 0.45f,
                                             (float)currentSampleRate * 0.49f);

            for (int ch = 0; ch < nCh; ++ch)
                bitAaFilter[ch].prepare(aaCut, (float)currentSampleRate);

            for (int ch = 0; ch < nCh; ++ch)
            {
                auto*  data = tempBuffer.getWritePointer(ch);
                float& cnt  = bitRateCounter[ch < 2 ? ch : 0];
                float& held = bitRateHeld   [ch < 2 ? ch : 0];

                for (int i = 0; i < ns; ++i)
                {
                    // Anti-alias before sample-hold (only meaningful when reducing rate)
                    const float filtered = ratioF > 1.0f ? bitAaFilter[ch].process(data[i])
                                                         : data[i];
                    cnt += 1.0f;
                    if (cnt >= ratioF)
                    {
                        // TPDF dither: two uniform samples → triangular distribution
                        const float r1 = rng.nextFloat();
                        const float r2 = rng.nextFloat();
                        held = std::round((filtered + (r1 - r2) * dither) * q) / q;
                        cnt -= ratioF;  // carry fraction for accurate rate reduction
                    }
                    data[i] = held;
                }
            }
            break;
        }
        case 5: // ── Clipper — hard-clip at threshold + post-output gain ─────
        {
            // driveDrive 0..100 → threshold % of full-scale (linear).
            // driveOutput -24..0 dB → post-clipper output gain.
            const float thresh  = juce::jlimit(0.001f, 1.0f, activeParams.driveDrive / 100.0f);
            const float outGain = std::pow(10.0f, activeParams.driveOutput / 20.0f);
            for (int ch = 0; ch < nCh; ++ch)
            {
                auto* data = tempBuffer.getWritePointer(ch);
                for (int i = 0; i < ns; ++i)
                    data[i] = juce::jlimit(-thresh, thresh, data[i]) * outGain;
            }
            break;
        }
        case 6: // ── 3-Band EQ: low shelf / mid peak / high shelf ─────────
        {
            using Coeffs = juce::dsp::IIR::Coefficients<float>;
            const float sr          = (float)currentSampleRate;
            const float curDriveDrv = activeParams.driveDrive;
            const float curDrvDit   = activeParams.drvDither;
            const float curMidGain  = activeParams.eqMidGain;
            const float curMidFreq  = juce::jlimit(20.0f, 20000.0f, activeParams.driveTone);

            // Only recompute when params actually change — avoids per-block heap allocation
            // from IIR::Coefficients::makeXxx() which creates a new ReferenceCountedObject.
            if (curDriveDrv != eqLastDriveDrive || curDrvDit  != eqLastDrvDither
             || curMidGain  != eqLastMidGain    || curMidFreq != eqLastDriveTone)
            {
                const float lowG  = juce::Decibels::decibelsToGain(curDriveDrv / 100.0f * 36.0f - 18.0f);
                const float highG = juce::Decibels::decibelsToGain(curDrvDit   / 100.0f * 36.0f - 18.0f);
                const float midG  = juce::Decibels::decibelsToGain(curMidGain);

                *eqLow .state = *Coeffs::makeLowShelf  (sr, 200.0f,    0.7f, lowG);
                *eqMid .state = *Coeffs::makePeakFilter(sr, curMidFreq, 1.0f, midG);
                *eqHigh.state = *Coeffs::makeHighShelf  (sr, 8000.0f,   0.7f, highG);

                eqLastDriveDrive = curDriveDrv;
                eqLastDrvDither  = curDrvDit;
                eqLastMidGain    = curMidGain;
                eqLastDriveTone  = curMidFreq;
            }

            eqLow .process(ctx);
            eqMid .process(ctx);
            eqHigh.process(ctx);
            break;
        }
        case 7: case 8: // ── Compressor / Limiter ──────────────────────────────
        {
            const float sr        = (float)currentSampleRate;
            const float threshLin = juce::Decibels::decibelsToGain(-(activeParams.driveDrive / 100.0f) * 40.0f);
            const float outGain   = juce::Decibels::decibelsToGain(activeParams.driveOutput);
            const float attackMs  = juce::jmax(0.1f, activeParams.drvDither * 2.0f);   // 0..100 → 0..200 ms
            const float relMs     = juce::jmax(1.0f,  activeParams.driveTone);          // stored directly as ms
            const float ratio     = (activeParams.driveChar == 8) ? 100.0f : 4.0f;
            const float attCoeff  = std::exp(-2.2f / (attackMs  * 0.001f * sr));
            const float relCoeff  = std::exp(-2.2f / (relMs     * 0.001f * sr));

            for (int ch = 0; ch < nCh; ++ch)
            {
                auto*  data = tempBuffer.getWritePointer(ch);
                float& env  = compEnvelope[ch < 2 ? ch : 0];
                for (int i = 0; i < ns; ++i)
                {
                    const float level = std::abs(data[i]);
                    env = level > env ? attCoeff * env + (1.0f - attCoeff) * level
                                      : relCoeff * env + (1.0f - relCoeff) * level;

                    float gainDb = 0.0f;
                    if (env > threshLin && threshLin > 1e-8f)
                    {
                        const float overDb = 20.0f * std::log10(env / threshLin);
                        gainDb = -overDb * (1.0f - 1.0f / ratio);
                    }
                    data[i] *= juce::Decibels::decibelsToGain(gainDb) * outGain;
                }
            }
            break;
        }
        default: break;  // 0 = None — bypass
    }

    // ── Tone filter (1-pole LP after drive, only for algorithms where driveTone = LP freq) ──
    // For EQ (6), Compressor (7), Limiter (8): driveTone stores mid freq / release ms, not an LPF.
    if (activeParams.driveChar < 6 && activeParams.driveTone < 19000.0f && currentSampleRate > 0.0)
    {
        for (int ch = 0; ch < nCh; ++ch)
            toneFilter[ch].prepare(activeParams.driveTone, (float)currentSampleRate);

        for (int ch = 0; ch < nCh; ++ch)
        {
            auto* data = tempBuffer.getWritePointer(ch);
            for (int i = 0; i < ns; ++i)
                data[i] = toneFilter[ch].process(data[i]);
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
