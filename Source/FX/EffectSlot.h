#pragma once

#include "FXSlotBase.h"
#include "FXAlgorithmDef.h"
#include "OversampledProcessor.h"
#include "DelaySlot.h"
#include "Effects/EffectAlgorithmBase.h"
#include "Effects/ChorusEffect.h"
#include "Effects/FlangerEffect.h"
#include "Effects/PhaserEffect.h"
#include "Effects/EchoEffect.h"

#include <array>
#include <memory>

// Hosts one of the 4 effect algorithms (Chorus, Flanger, Phaser, Echo).
// When algo = kEchoAlgoIndex (3), processing is delegated to an embedded DelaySlot
// so Echo mode has identical capabilities to the dedicated Delay unit.
//
// #402: all 4 algorithms are pre-allocated in the ctor and prepared together in
// prepare(). setAlgorithm() flips an index — no heap allocation. This matters
// because parameterChanged for `eff_algo` can fire on the audio thread when a
// DAW automates the parameter; the previous make_unique-on-setAlgorithm path
// was an audio-thread allocation.
class EffectSlot : public FXSlotBase
{
public:
    static constexpr int kNumAlgorithms = 4;
    static constexpr int kEchoAlgoIndex = 3;

    EffectSlot();

    void prepare(double sampleRate, int blockSize) override;
    void process(juce::AudioBuffer<float>&) override;

    juce::String getName()     override { return "Effect"; }
    juce::String getCategory() override { return "Send"; }
    juce::Component* createEditor() override { return nullptr; }
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    void setAlgorithm(int index);   // 0–3 — allocation-free; just flips active index
    int  getAlgorithmIndex() const { return algorithmIndex; }

    void setParam(const juce::String& id, float value);

    // Send-bus entry point — runs algorithm on the buffer (wet-only, no internal dry blend).
    void processReturn(juce::AudioBuffer<float>&);

    bool isEnabled() const  { return enabled; }
    void setEnabled(bool e) { enabled = e; }

    static const std::vector<FXAlgorithmDef>& allDefs() { return FXAlgorithmRegistry::effectAlgorithms(); }

    // Access the embedded DelaySlot used when algo == kEchoAlgoIndex.
    DelaySlot& getEchoDelay() { return echoDelay; }

    // True when the feedback loop must run every block regardless of input signal.
    bool needsContinuousProcessing() const
    {
        return algorithmIndex == kEchoAlgoIndex && enabled && echoDelay.isEnabled();
    }

private:
    // Pre-allocated in ctor — setAlgorithm() flips `algorithmIndex` into this array.
    std::array<std::unique_ptr<EffectAlgorithmBase>, kNumAlgorithms> algorithms;

    EffectAlgorithmBase* currentAlgorithm() const
    {
        return algorithms[(size_t) algorithmIndex].get();
    }

    std::unique_ptr<OversampledProcessor> oversampler;
    DelaySlot echoDelay;

    int    algorithmIndex = 0;
    bool   enabled        = true;
    double currentRate    = 44100.0;
    int    currentBlock   = 512;
};
