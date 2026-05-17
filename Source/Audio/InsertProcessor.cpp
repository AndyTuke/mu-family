#include "InsertProcessor.h"
#include <cmath>

void InsertProcessor::prepare(double sr, int blockSize)
{
    currentSampleRate = sr;
    toneFilter[0].reset();   toneFilter[1].reset();
    bitAaFilter[0].reset();  bitAaFilter[1].reset();
    prevDriveX[0] = prevDriveX[1] = 0.0f;
    bitRateCounter[0] = bitRateCounter[1] = 0.0f;
    bitRateHeld[0]    = bitRateHeld[1]    = 0.0f;
    compEnvelope[0]   = compEnvelope[1]   = 0.0f;
    ringPhase[0]      = ringPhase[1]      = 0.0f;
    dcBlockIn[0]      = dcBlockIn[1]      = 0.0f;
    dcBlockOut[0]     = dcBlockOut[1]     = 0.0f;
    eqLastDriveDrive = eqLastDrvDither = -1.0f;
    eqLastMidGain    = -999.0f;
    eqLastDriveTone  = -1.0f;
    const juce::dsp::ProcessSpec spec { sr, static_cast<uint32_t>(blockSize), 2 };
    eqLow.prepare(spec);
    eqMid.prepare(spec);
    eqHigh.prepare(spec);
}

void InsertProcessor::reset()
{
    toneFilter[0].reset();   toneFilter[1].reset();
    bitAaFilter[0].reset();  bitAaFilter[1].reset();
    prevDriveX[0] = prevDriveX[1] = 0.0f;
    bitRateCounter[0] = bitRateCounter[1] = 0.0f;
    bitRateHeld[0]    = bitRateHeld[1]    = 0.0f;
    compEnvelope[0]   = compEnvelope[1]   = 0.0f;
    ringPhase[0]      = ringPhase[1]      = 0.0f;
    dcBlockIn[0]      = dcBlockIn[1]      = 0.0f;
    dcBlockOut[0]     = dcBlockOut[1]     = 0.0f;
}

void InsertProcessor::process(juce::AudioBuffer<float>& buf, int ns, int nCh, const VoiceParams& p)
{
    grReduction.store(0.0f);  // reset; comp/limiter case overwrites with actual GR

    switch (p.driveChar)
    {
        case 1: // ── Soft Clip — tanh ADAA ────────────────────────────────────
        {
            const float preGain = std::pow(10.0f, p.driveDrive / 100.0f * 2.0f);
            const float outGain = std::pow(10.0f, p.driveOutput / 20.0f) / preGain;
            auto ad1Tanh = [](float x) -> float {
                return std::abs(x) > 12.0f ? std::abs(x) - 0.6931472f : std::log(std::cosh(x));
            };
            for (int ch = 0; ch < nCh; ++ch)
            {
                auto*  data  = buf.getWritePointer(ch);
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
        case 2: // ── Hard Clip — ADAA ─────────────────────────────────────────
        {
            const float preGain = std::pow(10.0f, p.driveDrive / 100.0f * 2.0f);
            const float outGain = std::pow(10.0f, p.driveOutput / 20.0f) / preGain;
            auto ad1Clip = [](float x) -> float {
                if (x >  1.0f) return x - 0.5f;
                if (x < -1.0f) return -x - 0.5f;
                return x * x * 0.5f;
            };
            for (int ch = 0; ch < nCh; ++ch)
            {
                auto*  data  = buf.getWritePointer(ch);
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
            const float preGain = std::pow(10.0f, p.driveDrive / 100.0f * 2.0f);
            const float outGain = std::pow(10.0f, p.driveOutput / 20.0f) / preGain;
            for (int ch = 0; ch < nCh; ++ch)
            {
                auto* data = buf.getWritePointer(ch);
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
            const float bits    = juce::jlimit(1.0f, 16.0f, p.drvBits);
            const float q       = std::pow(2.0f, bits - 1.0f);
            const float ratioF  = juce::jmax(1.0f,
                (float)(currentSampleRate / (double)juce::jmax(100.0f, p.driveRate)));
            const float dither  = p.drvDither / 100.0f * (0.5f / q);
            const float aaCut   = juce::jmin(p.driveRate * 0.45f,
                                             (float)currentSampleRate * 0.49f);

            for (int ch = 0; ch < nCh; ++ch)
                bitAaFilter[ch].prepare(aaCut, (float)currentSampleRate);

            for (int ch = 0; ch < nCh; ++ch)
            {
                auto*  data = buf.getWritePointer(ch);
                float& cnt  = bitRateCounter[ch < 2 ? ch : 0];
                float& held = bitRateHeld   [ch < 2 ? ch : 0];

                for (int i = 0; i < ns; ++i)
                {
                    const float filtered = ratioF > 1.0f ? bitAaFilter[ch].process(data[i])
                                                         : data[i];
                    cnt += 1.0f;
                    if (cnt >= ratioF)
                    {
                        const float r1 = rng.nextFloat();
                        const float r2 = rng.nextFloat();
                        held = std::round((filtered + (r1 - r2) * dither) * q) / q;
                        cnt -= ratioF;
                    }
                    data[i] = held;
                }
            }
            break;
        }
        case 5: // ── Clipper — hard-clip at threshold + post-output gain ─────
        {
            const float thresh  = juce::jlimit(0.001f, 1.0f, p.driveDrive / 100.0f);
            const float outGain = std::pow(10.0f, p.driveOutput / 20.0f);
            for (int ch = 0; ch < nCh; ++ch)
            {
                auto* data = buf.getWritePointer(ch);
                for (int i = 0; i < ns; ++i)
                    data[i] = juce::jlimit(-thresh, thresh, data[i]) * outGain;
            }
            break;
        }
        case 6: // ── 3-Band EQ: low shelf / mid peak / high shelf ─────────
        {
            using Coeffs = juce::dsp::IIR::Coefficients<float>;
            const float sr          = (float)currentSampleRate;
            const float curDriveDrv = p.driveDrive;
            const float curDrvDit   = p.drvDither;
            const float curMidGain  = p.eqMidGain;
            const float curMidFreq  = juce::jlimit(20.0f, 20000.0f, p.driveTone);

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

            const int nChClamped = juce::jmin(nCh, buf.getNumChannels());
            juce::dsp::AudioBlock<float> block(buf.getArrayOfWritePointers(),
                                               static_cast<size_t>(nChClamped),
                                               static_cast<size_t>(0),
                                               static_cast<size_t>(ns));
            juce::dsp::ProcessContextReplacing<float> ctx(block);
            eqLow .process(ctx);
            eqMid .process(ctx);
            eqHigh.process(ctx);
            break;
        }
        case 7: case 8: // ── Compressor / Limiter ──────────────────────────────
        {
            const float sr        = (float)currentSampleRate;
            const float threshLin = juce::Decibels::decibelsToGain(-(p.driveDrive / 100.0f) * 40.0f);
            const float outGain   = juce::Decibels::decibelsToGain(p.driveOutput);
            const float attackMs  = juce::jmax(0.1f, p.drvDither * 2.0f);
            const float relMs     = juce::jmax(1.0f, p.driveTone);
            const float ratio     = (p.driveChar == 8) ? 100.0f : 4.0f;
            const float attCoeff  = std::exp(-2.2f / (attackMs * 0.001f * sr));
            const float relCoeff  = std::exp(-2.2f / (relMs    * 0.001f * sr));

            float peakGainDb = 0.0f;  // 0 or negative; used for grReduction
            for (int ch = 0; ch < nCh; ++ch)
            {
                auto*  data = buf.getWritePointer(ch);
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
                    if (gainDb < peakGainDb) peakGainDb = gainDb;
                    data[i] *= juce::Decibels::decibelsToGain(gainDb) * outGain;
                }
            }
            // Normalise to 0..1 (1 ≡ 24 dB GR) for the UI meter.
            grReduction.store(juce::jlimit(0.0f, 1.0f, -peakGainDb / 24.0f));
            break;
        }
        case 9: // ── Ring Modulator: multiply by sine carrier ─────────────────
        {
            const float mix   = p.driveDrive / 100.0f;
            const float freq  = juce::jlimit(10.0f, 5000.0f, p.driveTone);
            const float phInc = 2.0f * juce::MathConstants<float>::pi * freq / (float)currentSampleRate;
            for (int ch = 0; ch < nCh; ++ch)
            {
                auto*  data = buf.getWritePointer(ch);
                float& ph   = ringPhase[ch < 2 ? ch : 0];
                for (int i = 0; i < ns; ++i)
                {
                    const float carrier = std::sin(ph);
                    ph += phInc;
                    if (ph >= juce::MathConstants<float>::twoPi)
                        ph -= juce::MathConstants<float>::twoPi;
                    data[i] = data[i] * (1.0f - mix + carrier * mix);
                }
            }
            break;
        }
        case 10: // ── Tape Saturation: gain → tanh → DC block → tone LP → output trim ──
        {
            const float preGain = 1.0f + (p.driveDrive / 100.0f) * 9.0f;  // 1..10×
            const float outGain = std::pow(10.0f, p.driveOutput / 20.0f);
            const float toneHz  = juce::jlimit(200.0f, 20000.0f, p.driveTone);
            const float dcCoeff = 1.0f - (2.0f * juce::MathConstants<float>::pi * 20.0f
                                          / (float)currentSampleRate);
            for (int ch = 0; ch < nCh; ++ch)
                toneFilter[ch].prepare(toneHz, (float)currentSampleRate);
            for (int ch = 0; ch < nCh; ++ch)
            {
                auto*  data = buf.getWritePointer(ch);
                float& dcIn  = dcBlockIn [ch < 2 ? ch : 0];
                float& dcOut = dcBlockOut[ch < 2 ? ch : 0];
                for (int i = 0; i < ns; ++i)
                {
                    const float sat = std::tanh(data[i] * preGain);
                    const float dc  = sat - dcIn + dcCoeff * dcOut;
                    dcIn  = sat;
                    dcOut = dc;
                    data[i] = toneFilter[ch].process(dc) * outGain;
                }
            }
            break;
        }
        default: break; // 0 = None — bypass
    }

    // Tone filter (1-pole LP after drive, only for algorithms where driveTone = LP cutoff freq).
    // EQ (6), Compressor (7), Limiter (8) repurpose driveTone for other meanings.
    if (p.driveChar < 6 && p.driveTone < 19000.0f && currentSampleRate > 0.0)
    {
        for (int ch = 0; ch < nCh; ++ch)
            toneFilter[ch].prepare(p.driveTone, (float)currentSampleRate);

        for (int ch = 0; ch < nCh; ++ch)
        {
            auto* data = buf.getWritePointer(ch);
            for (int i = 0; i < ns; ++i)
                data[i] = toneFilter[ch].process(data[i]);
        }
    }
}
