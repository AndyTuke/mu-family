#include "DelaySlot.h"
#include <algorithm>
#include <cmath>

DelaySlot::DelaySlot()
{
    bufL.assign((size_t) maxDelaySamples, 0.0f);
    bufR.assign((size_t) maxDelaySamples, 0.0f);
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

void DelaySlot::prepare(double sampleRate, int /*blockSize*/)
{
    sr = sampleRate;
    maxDelaySamples = juce::jmax(kBaseDelaySamples, (int) std::ceil(kMinDelaySeconds * sampleRate));
    bufL.assign((size_t) maxDelaySamples, 0.0f);
    bufR.assign((size_t) maxDelaySamples, 0.0f);
    writePosL = writePosR = 0;
    feedL = feedR = 0.0f;

    // 50 ms exponential smoothing to glide on delay time changes
    smoothCoeff = (float)std::exp(-1.0 / (0.050 * sampleRate));

    updateDelayFromMode();
    smoothedDelayL = targetDelayL.load(std::memory_order_relaxed);
    smoothedDelayR = targetDelayR.load(std::memory_order_relaxed);
}

void DelaySlot::reset()
{
    std::fill(bufL.begin(), bufL.end(), 0.0f);
    std::fill(bufR.begin(), bufR.end(), 0.0f);
    feedL = feedR = 0.0f;
}

void DelaySlot::process(juce::AudioBuffer<float>& buffer)
{
    // FXSlotBase contract — mu-clid uses send/return architecture, so the in-place
    // process() form just forwards to processReturn(). Retained for v3 plugin hosting.
    processReturn(buffer);
}

void DelaySlot::processReturn(juce::AudioBuffer<float>& buffer)
{
    // A disabled slot returns silence: the buffer arrives holding the dry send, which is already in
    // the main bus and must not be added back through the return.
    if (!enabled.load(std::memory_order_relaxed)) { buffer.clear(); return; }

    const int numCh      = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    auto* outL = (numCh > 0) ? buffer.getWritePointer(0) : nullptr;
    auto* outR = (numCh > 1) ? buffer.getWritePointer(1) : outL;

    // One snapshot of the targets per block; they glide through the smoothers below.
    const float tgtL = targetDelayL.load(std::memory_order_relaxed);
    const float tgtR = targetDelayR.load(std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i)
    {
        const float inL = (outL != nullptr) ? outL[i] : 0.0f;
        const float inR = (outR != nullptr) ? outR[i] : 0.0f;

        smoothedDelayL = smoothCoeff * smoothedDelayL + (1.0f - smoothCoeff) * tgtL;
        smoothedDelayR = smoothCoeff * smoothedDelayR + (1.0f - smoothCoeff) * tgtR;

        const float delayedL = hermiteDelay(bufL, writePosL, smoothedDelayL);
        const float delayedR = hermiteDelay(bufR, writePosR, smoothedDelayR);

        const float fb = feedback.load(std::memory_order_relaxed);
        feedL = processDirt(delayedL * fb);
        feedR = processDirt(delayedR * fb);

        bufL[writePosL] = inL + feedL;
        bufR[writePosR] = inR + feedR;

        writePosL = (writePosL + 1) % maxDelaySamples;
        writePosR = (writePosR + 1) % maxDelaySamples;

        if (outL != nullptr) outL[i] = delayedL;
        if (outR != nullptr) outR[i] = delayedR;
    }
}

void DelaySlot::setTimeCount(int count)
{
    syncCount.store(juce::jmax(1, count), std::memory_order_relaxed);
    if (timeMode.load(std::memory_order_relaxed) == TimeMode::Sync)
        updateDelayFromMode();
}

void DelaySlot::setDelayMs(float ms)
{
    delayMs.store(juce::jlimit(1.0f, 4000.0f, ms), std::memory_order_relaxed);
    if (timeMode.load(std::memory_order_relaxed) == TimeMode::Free)
        updateDelayFromMode();
}

void DelaySlot::setTimeDivision(int denominator, bool dotted, bool triplet)
{
    syncDenominator.store(denominator, std::memory_order_relaxed);
    syncDotted.store(dotted, std::memory_order_relaxed);
    syncTriplet.store(triplet, std::memory_order_relaxed);
    if (timeMode.load(std::memory_order_relaxed) == TimeMode::Sync)
        updateDelayFromMode();
}

void DelaySlot::updateDelayFromMode()
{
    float ms;
    if (timeMode.load(std::memory_order_relaxed) == TimeMode::Free)
    {
        ms = delayMs.load(std::memory_order_relaxed);
    }
    else
    {
        const double beatMs = 60000.0 / hostBpm.load(std::memory_order_relaxed);
        double noteMs = beatMs * 4.0 / syncDenominator.load(std::memory_order_relaxed);
        if (syncDotted.load(std::memory_order_relaxed))   noteMs *= 1.5;
        if (syncTriplet.load(std::memory_order_relaxed))  noteMs *= 2.0 / 3.0;
        noteMs *= syncCount.load(std::memory_order_relaxed);
        ms = static_cast<float>(noteMs);
    }

    const float sampL = static_cast<float>(ms * sr / 1000.0);
    const float sampR = sampL * (1.0f + spread.load(std::memory_order_relaxed) * 0.1f);
    setDelaySamplesLR(sampL, sampR);
}

void DelaySlot::setDelaySamplesLR(float sampL, float sampR)
{
    const float top = static_cast<float>(maxDelaySamples - 1);
    targetDelayL.store(juce::jlimit(1.0f, top, sampL), std::memory_order_relaxed);
    targetDelayR.store(juce::jlimit(1.0f, top, sampR), std::memory_order_relaxed);
}

float DelaySlot::processDirt(float x) const
{
    const float d = dirt.load(std::memory_order_relaxed);
    if (d < 0.001f) return x;
    const float gain = 1.0f + d * 8.0f;
    const float sat  = std::tanh(x * gain) / gain;
    return x + (sat - x) * d;
}
