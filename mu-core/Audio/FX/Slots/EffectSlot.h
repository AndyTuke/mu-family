#pragma once

#include "FXSlotBase.h"
#include "FXAlgorithmDef.h"
#include "OversampledProcessor.h"
#include "DelaySlot.h"
#include "Audio/FX/Send/EffectAlgorithmBase.h"
#include "Audio/FX/Send/ChorusEffect.h"
#include "Audio/FX/Send/FlangerEffect.h"
#include "Audio/FX/Send/PhaserEffect.h"

#include <array>
#include <atomic>
#include <memory>

// Hosts one of the 4 effect algorithms (Chorus, Flanger, Phaser, Echo).
// When algo = kEchoAlgoIndex (3), processing is delegated to an embedded DelaySlot
// so Echo mode has identical capabilities to the dedicated Delay unit.
//
// all 4 algorithms are pre-allocated in the ctor and prepared together in
// prepare(). setAlgorithm() flips an index — no heap allocation. This matters
// because parameterChanged for `eff_algo` can fire on the audio thread when a
// DAW automates the parameter; the previous make_unique-on-setAlgorithm path
// was an audio-thread allocation.
class EffectSlot : public FXSlotBase
{
public:
    static constexpr int kNumEffectAlgos = 4;
    static constexpr int kEchoAlgoIndex = 3;

    EffectSlot();

    void prepare(double sampleRate, int blockSize) override;
    void process(juce::AudioBuffer<float>&) override;

    juce::String getName()     override { return "Effect"; }
    juce::String getCategory() override { return "Send"; }
    juce::Component* createEditor() override { return nullptr; }
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    // Silence every algorithm's state and the embedded echo delay. Audio thread; no allocation.
    void reset();

    void setAlgorithm(int index);   // 0–3 — allocation-free; just flips active index
    int  getAlgorithmIndex() const { return algorithmIndex.load(std::memory_order_relaxed); }

    void setParam(const juce::String& id, float value);

    // Send-bus entry point — runs algorithm on the buffer (wet-only, no internal dry blend).
    void processReturn(juce::AudioBuffer<float>&);

    bool isEnabled() const  { return enabled.load(std::memory_order_relaxed); }
    void setEnabled(bool e) { enabled.store(e, std::memory_order_relaxed); }

    static const std::vector<FXAlgorithmDef>& allDefs() { return FXAlgorithmRegistry::effectAlgorithms(); }

    // Access the embedded DelaySlot used when algo == kEchoAlgoIndex.
    DelaySlot& getEchoDelay() { return echoDelay; }

    // True when the feedback loop must run every block regardless of input signal.
    bool needsContinuousProcessing() const
    {
        return algorithmIndex.load(std::memory_order_relaxed) == kEchoAlgoIndex
            && enabled.load(std::memory_order_relaxed)
            && echoDelay.isEnabled();
    }

private:
    // Pre-allocated in ctor — setAlgorithm() flips `algorithmIndex` into this array.
    // Chorus, Flanger and Phaser; the Echo index has no object here (the embedded DelaySlot runs it).
    std::array<std::unique_ptr<EffectAlgorithmBase>, kEchoAlgoIndex> algorithms;

    EffectAlgorithmBase* currentAlgorithm() const
    {
        const int i = algorithmIndex.load(std::memory_order_relaxed);
        return i >= 0 && i < kEchoAlgoIndex ? algorithms[(size_t) i].get() : nullptr;
    }

    std::unique_ptr<OversampledProcessor> oversampler;
    DelaySlot echoDelay;

    // Fields read on the audio thread are atomic — setEnabled / setAlgorithm can be
    // called from parameterChanged on any thread when a DAW automates eff_algo/bypass.
    std::atomic<int>  algorithmIndex { 0 };
    std::atomic<bool> enabled        { true };
    double currentRate    = 44100.0;
    int    currentBlock   = 512;
};
