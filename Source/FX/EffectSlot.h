#pragma once

#include "FXSlotBase.h"
#include "FXAlgorithmDef.h"
#include "DelaySlot.h"
#include "Effects/EffectAlgorithmBase.h"
#include "Effects/ChorusEffect.h"
#include "Effects/FlangerEffect.h"
#include "Effects/PhaserEffect.h"
#include "Effects/EchoEffect.h"

#include <memory>

// Hosts one of the 4 effect algorithms (Chorus, Flanger, Phaser, Echo).
//
// Parameter namespacing notes:
//   - Chorus/Flanger/Phaser use the generic `eff_p0..p4` APVTS slots, mapped
//     onto each algorithm's `FXAlgorithmDef::params` by index in
//     PluginProcessor::syncFXParam.
//   - Echo is the exception: when `algorithmIndex == kEchoAlgoIndex` the
//     processReturn path short-circuits to the embedded `echoDelay` (a
//     DelaySlot) and uses its own dedicated APVTS namespace —
//     `echo_ms`, `echo_fb`, `echo_spread`, `echo_dirt`, `echo_mode`,
//     `echo_syncDenom/Dot/Trip`, `echo_count`. This is what unlocks BPM-sync
//     and dirt for Echo (the registry-described Echo def has only
//     time/fb/spread/mix, no sync). The EchoEffect class itself is still
//     constructed by makeAlgorithm(3) but its processInner is never called.
class EffectSlot : public FXSlotBase
{
public:
    EffectSlot();

    void prepare(double sampleRate, int blockSize) override;
    void process(juce::AudioBuffer<float>&) override;

    juce::String getName()     override { return "Effect"; }
    juce::String getCategory() override { return "Send"; }
    juce::Component* createEditor() override { return nullptr; }
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    void setAlgorithm(int index);   // 0–3
    int  getAlgorithmIndex() const { return algorithmIndex; }

    void setParam(const juce::String& id, float value);

    // Send-bus entry point — runs algorithm on the buffer (wet-only, no internal dry blend).
    void processReturn(juce::AudioBuffer<float>&);

    bool isEnabled() const  { return enabled; }
    void setEnabled(bool e) { enabled = e; }

    static const std::vector<FXAlgorithmDef>& allDefs() { return FXAlgorithmRegistry::effectAlgorithms(); }

    static constexpr int kEchoAlgoIndex = 3;

    // Access the embedded DelaySlot used when algo == kEchoAlgoIndex.
    DelaySlot& getEchoDelay() { return echoDelay; }

private:
    std::unique_ptr<EffectAlgorithmBase> makeAlgorithm(int index);

    std::unique_ptr<EffectAlgorithmBase> algorithm;
    DelaySlot echoDelay;

    int    algorithmIndex = 0;
    bool   enabled        = true;
    double currentRate    = 44100.0;
    int    currentBlock   = 512;
};
