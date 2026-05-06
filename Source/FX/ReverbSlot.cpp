#include "ReverbSlot.h"
#include <signalsmith-basics/reverb.h>
#include <cmath>

// Pimpl: keeps Signalsmith headers (and their transitive windows.h) out of ReverbSlot.h.
struct ReverbSlot::ReverbImpl
{
    signalsmith::basics::ReverbFloat reverb;
};

ReverbSlot::ReverbSlot()
    : impl(std::make_unique<ReverbImpl>())
{
    preDelayBufL.assign(MaxPreDelaySamples, 0.0f);
    preDelayBufR.assign(MaxPreDelaySamples, 0.0f);
    applyAlgorithmPreset();
}

ReverbSlot::~ReverbSlot() = default;

void ReverbSlot::prepare(double sampleRate, int blockSize)
{
    sr = sampleRate;

    impl->reverb.configure(sampleRate, (size_t)blockSize, 2);

    const auto n = (size_t)blockSize;
    wetL.assign(n, 0.0f);
    wetR.assign(n, 0.0f);

    preDelayBufL.assign(MaxPreDelaySamples, 0.0f);
    preDelayBufR.assign(MaxPreDelaySamples, 0.0f);
    preDelayWrite = 0;

    updateReverb();
}

void ReverbSlot::runPreDelay(const juce::AudioBuffer<float>& src, int numSamples)
{
    const int numCh = src.getNumChannels();
    const auto* srcL = (numCh > 0) ? src.getReadPointer(0) : nullptr;
    const auto* srcR = (numCh > 1) ? src.getReadPointer(1) : srcL;

    const int delaySamples = juce::jlimit(0, MaxPreDelaySamples - 1,
        static_cast<int>(preDelay * sr / 1000.0));

    for (int i = 0; i < numSamples; ++i)
    {
        preDelayBufL[preDelayWrite] = srcL ? srcL[i] : 0.0f;
        preDelayBufR[preDelayWrite] = srcR ? srcR[i] : 0.0f;

        const int readPos = (preDelayWrite - delaySamples + MaxPreDelaySamples) % MaxPreDelaySamples;
        wetL[i] = preDelayBufL[readPos];
        wetR[i] = preDelayBufR[readPos];

        preDelayWrite = (preDelayWrite + 1) % MaxPreDelaySamples;
    }
}

void ReverbSlot::process(juce::AudioBuffer<float>& buffer)
{
    if (!enabled) return;

    const int numCh      = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    runPreDelay(buffer, numSamples);

    if (dirt > 0.001f)
    {
        const float gain = 1.0f + dirt * 4.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            wetL[i] = std::tanh(wetL[i] * gain) / gain;
            wetR[i] = std::tanh(wetR[i] * gain) / gain;
        }
    }

    float* ins[2]  = { wetL.data(), wetR.data() };
    float* outs[2] = { wetL.data(), wetR.data() };
    impl->reverb.process(ins, outs, (size_t)numSamples);

    // Add wet reverb to output (send-style — dry signal is unaffected).
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* out = buffer.getWritePointer(ch);
        const auto* wet = (ch == 0) ? wetL.data() : wetR.data();
        for (int i = 0; i < numSamples; ++i)
            out[i] += wet[i] * level;
    }
}

void ReverbSlot::processReturn(juce::AudioBuffer<float>& buffer)
{
    if (!enabled) return;

    const int numCh      = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    runPreDelay(buffer, numSamples);

    if (dirt > 0.001f)
    {
        const float gain = 1.0f + dirt * 4.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            wetL[i] = std::tanh(wetL[i] * gain) / gain;
            wetR[i] = std::tanh(wetR[i] * gain) / gain;
        }
    }

    float* ins[2]  = { wetL.data(), wetR.data() };
    float* outs[2] = { wetL.data(), wetR.data() };
    impl->reverb.process(ins, outs, (size_t)numSamples);

    // Overwrite buffer with wet-only output (dry send is already in the main mix).
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* out = buffer.getWritePointer(ch);
        const auto* wet = (ch == 0) ? wetL.data() : wetR.data();
        for (int i = 0; i < numSamples; ++i)
            out[i] = wet[i] * level;
    }
}

void ReverbSlot::setAlgorithm(int index)
{
    algorithmIndex = juce::jlimit(0, 3, index);
    applyAlgorithmPreset();
    updateReverb();
}

void ReverbSlot::setParam(const juce::String& id, float value)
{
    if      (id == "size")      size      = value;
    else if (id == "predelay")  preDelay  = value;
    else if (id == "diffusion") diffusion = value;
    else if (id == "damp")      damp      = value;
    else if (id == "mod")       mod       = value;
    else if (id == "dirt")      dirt      = value;
    updateReverb();
}

void ReverbSlot::applyAlgorithmPreset()
{
    switch (static_cast<Algorithm>(algorithmIndex))
    {
        case Algorithm::Room:
            size = 0.25f; rt20 = 0.6f;  damp = 0.6f; diffusion = 0.6f; preDelay = 5.0f;  mod = 0.10f;
            break;
        case Algorithm::Hall:
            size = 0.75f; rt20 = 2.5f;  damp = 0.3f; diffusion = 0.8f; preDelay = 25.0f; mod = 0.15f;
            break;
        case Algorithm::Plate:
            size = 0.45f; rt20 = 1.8f;  damp = 0.5f; diffusion = 0.9f; preDelay = 10.0f; mod = 0.30f;
            break;
        case Algorithm::Spring:
            size = 0.15f; rt20 = 0.35f; damp = 0.8f; diffusion = 0.3f; preDelay = 2.0f;  mod = 0.60f;
            break;
    }
}

void ReverbSlot::updateReverb()
{
    auto& r = impl->reverb;

    r.dry = 0.0;
    r.wet = 1.0;

    // size [0,1] → roomMs [10, 200] ms
    r.roomMs       = 10.0 + (double)size * 190.0;
    r.rt20         = (double)rt20;

    // diffusion [0,1] → early reflections strength [0, 2.5]
    r.early        = (double)diffusion * 2.5;

    // damp [0,1] → high-frequency decay rate [1, 9] (higher = more damped)
    r.highDampRate = 1.0 + (double)damp * 8.0;
    r.lowDampRate  = 1.0 + (double)damp * 1.5;

    // mod [0,1] → per-line detuning amount [0, 30]
    r.detune       = (double)mod * 30.0;

    // Fixed spectral shape per algorithm
    switch (static_cast<Algorithm>(algorithmIndex))
    {
        case Algorithm::Room:
            r.lowCutHz = 100.0; r.highCutHz = 8000.0;  break;
        case Algorithm::Hall:
            r.lowCutHz = 60.0;  r.highCutHz = 12000.0; break;
        case Algorithm::Plate:
            r.lowCutHz = 150.0; r.highCutHz = 10000.0; break;
        case Algorithm::Spring:
            r.lowCutHz = 200.0; r.highCutHz = 5500.0;  break;
    }
}
