#include "DelaySlot.h"
#include <cmath>

DelaySlot::DelaySlot()
{
    bufL.assign(MaxDelaySamples, 0.0f);
    bufR.assign(MaxDelaySamples, 0.0f);
}

// Hermite cubic interpolation for fractional delay reads.
static float hermiteDelay(const std::vector<float>& buf, int writePos, float delaySamples)
{
    const int   bufSize = static_cast<int>(buf.size());
    const float d       = juce::jlimit(2.0f, static_cast<float>(bufSize - 2), delaySamples);
    const int   di      = static_cast<int>(d);
    const float frac    = d - static_cast<float>(di);

    const int r0  = (writePos - di     + bufSize) % bufSize;  // x[0]
    const int rm1 = (writePos - di + 1 + bufSize) % bufSize;  // x[-1] (one newer)
    const int r1  = (writePos - di - 1 + bufSize) % bufSize;  // x[1]  (one older)
    const int r2  = (writePos - di - 2 + bufSize) % bufSize;  // x[2]  (two older)

    const float xm1 = buf[rm1];
    const float x0  = buf[r0];
    const float x1  = buf[r1];
    const float x2  = buf[r2];

    const float c0 = x0;
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);

    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}

void DelaySlot::prepare(double sampleRate, int blockSize)
{
    sr = sampleRate;
    bufL.assign(MaxDelaySamples, 0.0f);
    bufR.assign(MaxDelaySamples, 0.0f);
    writePosL = writePosR = 0;
    feedL = feedR = 0.0f;
    dampStateL = dampStateR = 0.0f;
    juce::ignoreUnused(blockSize);

    // 50 ms exponential smoothing to glide on delay time changes
    smoothCoeff = (float)std::exp(-1.0 / (0.050 * sampleRate));

    updateDelayFromMode();
    updateDampCoeff();
    smoothedDelayL = targetDelayL;
    smoothedDelayR = targetDelayR;
}

void DelaySlot::process(juce::AudioBuffer<float>& buffer)
{
    // Insert-style path is no longer wired up by FXChain — mixer uses
    // processReturn (send-bus, wet-only). Forwarded for FXSlotBase contract.
    processReturn(buffer);
}

void DelaySlot::processReturn(juce::AudioBuffer<float>& buffer)
{
    if (!enabled) return;

    const int numCh      = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    auto* outL = (numCh > 0) ? buffer.getWritePointer(0) : nullptr;
    auto* outR = (numCh > 1) ? buffer.getWritePointer(1) : outL;

    for (int i = 0; i < numSamples; ++i)
    {
        const float inL = (outL != nullptr) ? outL[i] : 0.0f;
        const float inR = (outR != nullptr) ? outR[i] : 0.0f;

        smoothedDelayL = smoothCoeff * smoothedDelayL + (1.0f - smoothCoeff) * targetDelayL;
        smoothedDelayR = smoothCoeff * smoothedDelayR + (1.0f - smoothCoeff) * targetDelayR;

        float delayedL = hermiteDelay(bufL, writePosL, smoothedDelayL);
        float delayedR = hermiteDelay(bufR, writePosR, smoothedDelayR);

        // Damping LP in the feedback path — only the recirculated signal is
        // filtered, so the first echo retains its full brightness and each
        // successive repeat dulls progressively (tape-style HF decay).
        if (damp > 0.001f)
        {
            dampStateL = (1.0f - dampCoeff) * delayedL + dampCoeff * dampStateL;
            dampStateR = (1.0f - dampCoeff) * delayedR + dampCoeff * dampStateR;
        }
        else
        {
            dampStateL = delayedL;
            dampStateR = delayedR;
        }

        feedL = processDirt(dampStateL * feedback);
        feedR = processDirt(dampStateR * feedback);

        bufL[writePosL] = inL + feedL;
        bufR[writePosR] = inR + feedR;

        writePosL = (writePosL + 1) % MaxDelaySamples;
        writePosR = (writePosR + 1) % MaxDelaySamples;

        if (outL != nullptr) outL[i] = delayedL;
        if (outR != nullptr) outR[i] = delayedR;
    }
}

void DelaySlot::setTimeCount(int count)
{
    syncCount = juce::jmax(1, count);
    if (timeMode == TimeMode::Sync)
        updateDelayFromMode();
}

void DelaySlot::setDelayMs(float ms)
{
    delayMs = juce::jlimit(1.0f, 4000.0f, ms);
    if (timeMode == TimeMode::Free)
        updateDelayFromMode();
}

void DelaySlot::setTimeDivision(int denominator, bool dotted, bool triplet)
{
    syncDenominator = denominator;
    syncDotted      = dotted;
    syncTriplet     = triplet;
    if (timeMode == TimeMode::Sync)
        updateDelayFromMode();
}

void DelaySlot::updateDelayFromMode()
{
    float ms;
    if (timeMode == TimeMode::Free)
    {
        ms = delayMs;
    }
    else
    {
        const double beatMs = 60000.0 / hostBpm;
        double noteMs = beatMs * 4.0 / syncDenominator;
        if (syncDotted)   noteMs *= 1.5;
        if (syncTriplet)  noteMs *= 2.0 / 3.0;
        noteMs *= syncCount;
        ms = static_cast<float>(noteMs);
    }

    const float sampL = static_cast<float>(ms * sr / 1000.0);
    const float sampR = sampL * (1.0f + spread * 0.1f);
    setDelaySamplesLR(sampL, sampR);
}

void DelaySlot::setDelaySamplesLR(float sampL, float sampR)
{
    targetDelayL = juce::jlimit(1.0f, static_cast<float>(MaxDelaySamples - 1), sampL);
    targetDelayR = juce::jlimit(1.0f, static_cast<float>(MaxDelaySamples - 1), sampR);
}

float DelaySlot::processDirt(float x) const
{
    if (dirt < 0.001f) return x;
    const float gain = 1.0f + dirt * 8.0f;
    const float sat  = std::tanh(x * gain) / gain;
    return x + (sat - x) * dirt;
}

void DelaySlot::updateDampCoeff()
{
    // Map damp [0, 1] to 1-pole LP cutoff [20 kHz, 800 Hz] geometrically.
    // Coefficient form: y[n] = (1 - a)·x[n] + a·y[n-1], a = exp(-2π·fc/sr).
    // At damp = 0 the filter is essentially flat (a → 0); at damp = 1 the
    // feedback path rolls off above ~800 Hz, giving the tape-style HF decay.
    const float fcMax = 20000.0f;
    const float fcMin =   800.0f;
    const float fc    = fcMax * std::pow(fcMin / fcMax, damp);
    dampCoeff = std::exp(-juce::MathConstants<float>::twoPi * fc / (float)sr);
}
