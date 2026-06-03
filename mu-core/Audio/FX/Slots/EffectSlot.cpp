#include "EffectSlot.h"

EffectSlot::EffectSlot()
{
    // pre-allocate all algorithms up-front so setAlgorithm() never heap-allocates.
    // Each is small (delay-line vectors, LFO state) — the combined footprint is bounded.
    // All start in send-mode (the slot is wired into the mixer's send/return path).
    // algorithmIndex stays at 0 (Chorus) until setAlgorithm flips it.
    algorithms[0] = std::make_unique<ChorusEffect>();
    algorithms[1] = std::make_unique<FlangerEffect>();
    algorithms[2] = std::make_unique<PhaserEffect>();
    algorithms[3] = std::make_unique<EchoEffect>();
    for (auto& a : algorithms)
        a->setSendMode(true);
}

void EffectSlot::prepare(double sampleRate, int blockSize)
{
    currentRate  = sampleRate;
    currentBlock = blockSize;

    oversampler = std::make_unique<OversampledProcessor>(1);
    oversampler->prepare(sampleRate, blockSize);

    // prepare every algorithm — they're all live, just inactive. Subsequent
    // setAlgorithm calls do NOT re-prepare (would alloc); state from prior activation
    // persists. The artifact of stale state on swap is a minor click compared to the
    // audio-thread allocation it replaces.
    for (auto& a : algorithms)
        if (a) a->prepareInner(sampleRate, blockSize);

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
    // allocation-free — just flips the active index into the pre-allocated array.
    // Safe to call from the audio thread (DAW host automation on `eff_algo`).
    algorithmIndex.store(juce::jlimit(0, kNumEffectAlgos - 1, index), std::memory_order_relaxed);
}

void EffectSlot::setParam(const juce::String& id, float value)
{
    if (auto* a = currentAlgorithm())
        a->setParam(id, value);
}

void EffectSlot::processReturn(juce::AudioBuffer<float>& buffer)
{
    if (!enabled.load(std::memory_order_relaxed)) return;
    if (algorithmIndex.load(std::memory_order_relaxed) == kEchoAlgoIndex)
    {
        echoDelay.processReturn(buffer);
        return;
    }
    auto* algo = currentAlgorithm();
    if (!algo) return;
    oversampler->process(buffer, [algo](juce::dsp::AudioBlock<float>& block)
    {
        algo->processInner(block);
    });
}
