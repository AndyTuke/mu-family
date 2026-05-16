#include "EffectSlot.h"

EffectSlot::EffectSlot()
{
    setAlgorithm(0);
}

void EffectSlot::prepare(double sampleRate, int blockSize)
{
    currentRate  = sampleRate;
    currentBlock = blockSize;

    oversampler = std::make_unique<OversampledProcessor>(1);
    oversampler->prepare(sampleRate, blockSize);

    if (algorithm)
        algorithm->prepareInner(sampleRate, blockSize);

    echoDelay.prepare(sampleRate, blockSize);
}

void EffectSlot::process(juce::AudioBuffer<float>& buffer)
{
    // FXSlotBase contract — mu-clid uses send/return architecture, so the in-place
    // process() form just forwards to processReturn(). Retained for v3 plugin hosting.
    processReturn(buffer);
}

void EffectSlot::setAlgorithm(int index)
{
    algorithmIndex = juce::jlimit(0, 3, index);
    algorithm      = makeAlgorithm(algorithmIndex);
    if (algorithm) algorithm->setSendMode(true); // Issue #44: Effect slot is wired into mixer's send/return — wet-only

    if (currentRate > 0.0)
    {
        oversampler = std::make_unique<OversampledProcessor>(1);
        oversampler->prepare(currentRate, currentBlock);
        algorithm->prepareInner(currentRate, currentBlock);
    }
}

void EffectSlot::setParam(const juce::String& id, float value)
{
    if (algorithm)
        algorithm->setParam(id, value);
}

void EffectSlot::processReturn(juce::AudioBuffer<float>& buffer)
{
    if (!enabled) return;
    if (algorithmIndex == kEchoAlgoIndex)
    {
        echoDelay.processReturn(buffer);
        return;
    }
    if (!algorithm) return;
    oversampler->process(buffer, [this](juce::dsp::AudioBlock<float>& block)
    {
        algorithm->processInner(block);
    });
}

std::unique_ptr<EffectAlgorithmBase> EffectSlot::makeAlgorithm(int index)
{
    switch (index)
    {
        case 0: return std::make_unique<ChorusEffect>();
        case 1: return std::make_unique<FlangerEffect>();
        case 2: return std::make_unique<PhaserEffect>();
        case 3: return std::make_unique<EchoEffect>();
        default: return std::make_unique<ChorusEffect>();
    }
}
