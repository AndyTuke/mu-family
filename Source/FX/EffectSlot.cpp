#include "EffectSlot.h"

EffectSlot::EffectSlot()
{
    setAlgorithm(0);
}

void EffectSlot::prepare(double sampleRate, int blockSize)
{
    currentRate  = sampleRate;
    currentBlock = blockSize;

    const int factor = algorithm ? algorithm->getDef().oversamplingFactor : 1;
    oversampler = std::make_unique<OversampledProcessor>(factor);
    oversampler->prepare(sampleRate, blockSize);

    const double osRate = oversampler->getOversampledRate();
    if (algorithm)
        algorithm->prepareInner(osRate, blockSize * factor);

    dryBuffer.setSize(2, blockSize);
}

void EffectSlot::process(juce::AudioBuffer<float>& buffer)
{
    if (!enabled || !algorithm)
        return;

    // Capture dry signal.
    dryBuffer.makeCopyOf(buffer, true);

    oversampler->process(buffer, [this](juce::dsp::AudioBlock<float>& block)
    {
        algorithm->processInner(block);
    });

    // Insert-style blending:
    //   send 0.0  → 100% dry,   0% wet
    //   send 0.5  → 100% dry, 100% wet  (parallel)
    //   send 1.0  →   0% dry, 100% wet  (full wet)
    const int numCh      = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    float dryGain, wetGain;
    if (sendAmount <= 0.5f)
    {
        dryGain = 1.0f;
        wetGain = sendAmount * 2.0f;
    }
    else
    {
        dryGain = 1.0f - (sendAmount - 0.5f) * 2.0f;
        wetGain = 1.0f;
    }

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* wet = buffer.getWritePointer(ch);
        auto* dry = dryBuffer.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
            wet[i] = dry[i] * dryGain + wet[i] * wetGain;
    }
}

void EffectSlot::setAlgorithm(int index)
{
    algorithmIndex = juce::jlimit(0, 7, index);
    algorithm      = makeAlgorithm(algorithmIndex);

    if (currentRate > 0.0)
    {
        const int factor = algorithm->getDef().oversamplingFactor;
        oversampler = std::make_unique<OversampledProcessor>(factor);
        oversampler->prepare(currentRate, currentBlock);
        algorithm->prepareInner(oversampler->getOversampledRate(), currentBlock * factor);
    }
}

void EffectSlot::setParam(const juce::String& id, float value)
{
    if (algorithm)
        algorithm->setParam(id, value);
}

void EffectSlot::setSend(float amount)
{
    sendAmount = juce::jlimit(0.0f, 1.0f, amount);
}

std::unique_ptr<EffectAlgorithmBase> EffectSlot::makeAlgorithm(int index)
{
    switch (index)
    {
        case 0: return std::make_unique<SoftClipEffect>();
        case 1: return std::make_unique<HardClipEffect>();
        case 2: return std::make_unique<FoldbackEffect>();
        case 3: return std::make_unique<BitcrushEffect>();
        case 4: return std::make_unique<LadderFilterEffect>();
        case 5: return std::make_unique<ChorusEffect>();
        case 6: return std::make_unique<PhaserEffect>();
        case 7: return std::make_unique<CombFilterEffect>();
        default: return std::make_unique<SoftClipEffect>();
    }
}
