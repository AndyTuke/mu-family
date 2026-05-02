#include "ReverbSlot.h"
#include <cmath>

ReverbSlot::ReverbSlot()
{
    preDelayBufL.assign(MaxPreDelaySamples, 0.0f);
    preDelayBufR.assign(MaxPreDelaySamples, 0.0f);
    applyAlgorithmPreset();
    updateReverb();
}

void ReverbSlot::prepare(double sampleRate, int /*blockSize*/)
{
    sr = sampleRate;
    reverb.setSampleRate(sampleRate);
    reverb.setParameters(reverbParams);

    preDelayBufL.assign(MaxPreDelaySamples, 0.0f);
    preDelayBufR.assign(MaxPreDelaySamples, 0.0f);
    preDelayWrite = 0;
}

void ReverbSlot::process(juce::AudioBuffer<float>& buffer)
{
    if (!enabled) return;

    const int numCh      = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    // Pre-delay: route input through delay buffer before reverb.
    const int preDelaySamples = juce::jlimit(0, MaxPreDelaySamples - 1,
        static_cast<int>(preDelay * sr / 1000.0));

    juce::AudioBuffer<float> reverbInput(2, numSamples);
    reverbInput.clear();

    auto* srcL = (numCh > 0) ? buffer.getReadPointer(0) : nullptr;
    auto* srcR = (numCh > 1) ? buffer.getReadPointer(1) : srcL;
    auto* rvL  = reverbInput.getWritePointer(0);
    auto* rvR  = reverbInput.getWritePointer(1);

    for (int i = 0; i < numSamples; ++i)
    {
        preDelayBufL[preDelayWrite] = (srcL != nullptr) ? srcL[i] : 0.0f;
        preDelayBufR[preDelayWrite] = (srcR != nullptr) ? srcR[i] : 0.0f;

        const int readPos = (preDelayWrite - preDelaySamples + MaxPreDelaySamples) % MaxPreDelaySamples;
        rvL[i] = preDelayBufL[readPos];
        rvR[i] = preDelayBufR[readPos];

        preDelayWrite = (preDelayWrite + 1) % MaxPreDelaySamples;
    }

    // Apply dirt (mild saturation before reverb for character).
    if (dirt > 0.001f)
    {
        const float gain = 1.0f + dirt * 4.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            rvL[i] = std::tanh(rvL[i] * gain) / gain;
            rvR[i] = std::tanh(rvR[i] * gain) / gain;
        }
    }

    // Process through Freeverb reverb (wet only).
    reverb.processStereo(rvL, rvR, numSamples);

    // Add reverb wet to output (send-style — dry is unaffected).
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* out  = buffer.getWritePointer(ch);
        auto* wet  = reverbInput.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
            out[i] += wet[i] * level;
    }
}

void ReverbSlot::processReturn(juce::AudioBuffer<float>& buffer)
{
    if (!enabled) return;

    const int numCh      = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    const int preDelaySamples = juce::jlimit(0, MaxPreDelaySamples - 1,
        static_cast<int>(preDelay * sr / 1000.0));

    juce::AudioBuffer<float> reverbInput(2, numSamples);
    reverbInput.clear();

    auto* srcL = (numCh > 0) ? buffer.getReadPointer(0) : nullptr;
    auto* srcR = (numCh > 1) ? buffer.getReadPointer(1) : srcL;
    auto* rvL  = reverbInput.getWritePointer(0);
    auto* rvR  = reverbInput.getWritePointer(1);

    for (int i = 0; i < numSamples; ++i)
    {
        preDelayBufL[preDelayWrite] = (srcL != nullptr) ? srcL[i] : 0.0f;
        preDelayBufR[preDelayWrite] = (srcR != nullptr) ? srcR[i] : 0.0f;

        const int readPos = (preDelayWrite - preDelaySamples + MaxPreDelaySamples) % MaxPreDelaySamples;
        rvL[i] = preDelayBufL[readPos];
        rvR[i] = preDelayBufR[readPos];

        preDelayWrite = (preDelayWrite + 1) % MaxPreDelaySamples;
    }

    if (dirt > 0.001f)
    {
        const float gain = 1.0f + dirt * 4.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            rvL[i] = std::tanh(rvL[i] * gain) / gain;
            rvR[i] = std::tanh(rvR[i] * gain) / gain;
        }
    }

    reverb.processStereo(rvL, rvR, numSamples);

    // Overwrite buffer with wet-only output (dry send is already in the main mix).
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* out = buffer.getWritePointer(ch);
        auto* wet = reverbInput.getReadPointer(ch);
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
    // Each algorithm biases the base parameters for a different character.
    switch (static_cast<Algorithm>(algorithmIndex))
    {
        case Algorithm::Room:
            size = 0.35f; damp = 0.6f; diffusion = 0.6f; preDelay = 5.0f;
            break;
        case Algorithm::Hall:
            size = 0.85f; damp = 0.3f; diffusion = 0.8f; preDelay = 25.0f;
            break;
        case Algorithm::Plate:
            size = 0.6f;  damp = 0.5f; diffusion = 0.9f; preDelay = 10.0f;
            break;
        case Algorithm::Spring:
            size = 0.2f;  damp = 0.8f; diffusion = 0.4f; preDelay = 2.0f;
            break;
    }
}

void ReverbSlot::updateReverb()
{
    // Map our unified params onto juce::dsp::Reverb::Parameters.
    reverbParams.roomSize   = size;
    reverbParams.damping    = damp;
    reverbParams.width      = diffusion;
    reverbParams.wetLevel   = 1.0f;
    reverbParams.dryLevel   = 0.0f;
    reverbParams.freezeMode = 0.0f;
    reverb.setParameters(reverbParams);
}
