#pragma once

#include "FXSlotBase.h"
#include "FXAlgorithmDef.h"
#include "OversampledProcessor.h"
#include "Effects/EffectAlgorithmBase.h"
#include "Effects/SoftClipEffect.h"
#include "Effects/HardClipEffect.h"
#include "Effects/FoldbackEffect.h"
#include "Effects/BitcrushEffect.h"
#include "Effects/LadderFilterEffect.h"
#include "Effects/ChorusEffect.h"
#include "Effects/PhaserEffect.h"
#include "Effects/CombFilterEffect.h"

#include <memory>

// Hosts one of the 8 effect algorithms with insert-style wet/dry blending.
// Insert blend: 0–50% → blend wet in (full dry always present).
//               50–100% → fade dry out (full wet always present).
class EffectSlot : public FXSlotBase
{
public:
    EffectSlot();

    void prepare(double sampleRate, int blockSize) override;
    void process(juce::AudioBuffer<float>&) override;

    juce::String getName()     override { return "Effect"; }
    juce::String getCategory() override { return "Insert"; }
    juce::Component* createEditor() override { return nullptr; }
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    void setAlgorithm(int index);   // 0–7
    int  getAlgorithmIndex() const { return algorithmIndex; }

    void setParam(const juce::String& id, float value);
    void setSend(float sendAmount);  // 0.0 = dry, 1.0 = full wet

    // Send-bus processing: applies algorithm with no dry/wet blend (wet-only output).
    void processReturn(juce::AudioBuffer<float>&);

    bool isEnabled() const  { return enabled; }
    void setEnabled(bool e) { enabled = e; }

    static std::vector<FXAlgorithmDef> allDefs() { return FXAlgorithmRegistry::effectAlgorithms(); }

private:
    std::unique_ptr<EffectAlgorithmBase> makeAlgorithm(int index);

    std::unique_ptr<EffectAlgorithmBase> algorithm;
    std::unique_ptr<OversampledProcessor> oversampler;
    juce::AudioBuffer<float> dryBuffer;

    int    algorithmIndex = 0;
    float  sendAmount     = 1.0f;
    bool   enabled        = true;
    double currentRate    = 44100.0;
    int    currentBlock   = 512;
};
