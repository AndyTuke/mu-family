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

    // Warm up each band's coefficient storage to flat (gain=1.0) so the first
    // ArrayCoefficients::assign call inside process() doesn't have to grow the
    // internal Array<float>. After this, subsequent updates reuse the storage.
    using ArrayCoeffs = juce::dsp::IIR::ArrayCoefficients<float>;
    *eqLow .state = ArrayCoeffs::makeLowShelf  (sr,  200.0f, 0.7f, 1.0f);
    *eqMid .state = ArrayCoeffs::makePeakFilter(sr, 1000.0f, 1.0f, 1.0f);
    *eqHigh.state = ArrayCoeffs::makeHighShelf (sr, 8000.0f, 0.7f, 1.0f);
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
    switch (p.driveChar)
    {
        case 1: // ── Soft Clip — tanh ADAA ────────────────────────────────────
        {
            // Drive curve: pow(10, drive/100 * 1.2) gives 0..+24 dB pre-gain at
            // drive 0..100. The previous +40 dB ceiling pushed everything into
            // saturation in the upper half of the knob with no audible change;
            // +24 dB keeps the full sweep musically useful per Faust libs /
            // Jatin Chowdhury references. Tape Sat uses its own 0..+20 dB curve.
            const float preGain = std::pow(10.0f, p.driveDrive / 100.0f * 1.2f);
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
            const float preGain = std::pow(10.0f, p.driveDrive / 100.0f * 1.2f);  // 0..+24 dB
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
            const float preGain = std::pow(10.0f, p.driveDrive / 100.0f * 1.2f);  // 0..+24 dB
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

            // Anti-alias filter: when on, cutoff floors at 2 kHz so the audible
            // band survives even at extreme rate reduction (driveRate→100 Hz used
            // to cut at 45 Hz and kill the signal). When off, the bitcrusher
            // produces raw aliasing — the classic gritty/glitchy character that
            // many lo-fi bitcrushers (Kilohearts Bitcrush, Tritik Krush) expose
            // explicitly. Defaults to on.
            const bool  aaOn    = p.drvAa;
            const float aaCut   = juce::jmin(juce::jmax(2000.0f, p.driveRate * 0.45f),
                                             (float)currentSampleRate * 0.49f);

            if (aaOn)
                for (int ch = 0; ch < nCh; ++ch)
                    bitAaFilter[ch].prepare(aaCut, (float)currentSampleRate);

            for (int ch = 0; ch < nCh; ++ch)
            {
                auto*  data = buf.getWritePointer(ch);
                float& cnt  = bitRateCounter[ch < 2 ? ch : 0];
                float& held = bitRateHeld   [ch < 2 ? ch : 0];

                for (int i = 0; i < ns; ++i)
                {
                    const float filtered = (aaOn && ratioF > 1.0f)
                                            ? bitAaFilter[ch].process(data[i])
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
            // ArrayCoefficients returns a std::array<float, 6> on the stack;
            // assigning it to *eqLow.state goes through Coefficients::operator=
            // which writes into the existing Array<float> storage rather than
            // calling `new Coefficients(...)` like the Ptr-returning helpers do.
            using ArrayCoeffs = juce::dsp::IIR::ArrayCoefficients<float>;
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

                // Shelf corner frequencies are intentionally fixed (low @ 200 Hz,
                // high @ 8 kHz). The 3-Band EQ is a "character EQ" — three controls
                // already (low gain / mid freq+gain / high gain) — so exposing
                // shelf-frequency knobs would push this past the 3-knob insert UI
                // slot. Mid frequency stays sweepable via `driveTone`.
                *eqLow .state = ArrayCoeffs::makeLowShelf  (sr,  200.0f,     0.7f, lowG);
                *eqMid .state = ArrayCoeffs::makePeakFilter(sr,  curMidFreq, 1.0f, midG);
                *eqHigh.state = ArrayCoeffs::makeHighShelf (sr, 8000.0f,     0.7f, highG);

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
                    data[i] *= juce::Decibels::decibelsToGain(gainDb) * outGain;
                }
            }
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
    // Skip when driveChar == 0 (None / bypass) so dialling driveTone for a previous
    // algorithm and then switching back to None doesn't silently dull the dry signal.
    // EQ (6), Compressor (7), Limiter (8) repurpose driveTone for other meanings.
    if (p.driveChar >= 1 && p.driveChar < 6 && p.driveTone < 19000.0f && currentSampleRate > 0.0)
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
