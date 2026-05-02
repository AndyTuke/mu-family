#pragma once

#include "FXSlotBase.h"
#include "FXAlgorithmDef.h"
#include <juce_dsp/juce_dsp.h>
#include <vector>

// Send-style reverb slot. Dry signal is unaffected — wet reverb is added on top.
// Currently implemented with juce::dsp::Reverb (Freeverb). Algorithm parameter
// presets differentiate Room / Hall / Plate / Spring character.
// Replace with Signalsmith Reverb (MIT, header-only) in a future stage.
class ReverbSlot : public FXSlotBase
{
public:
    enum class Algorithm { Room = 0, Hall, Plate, Spring };

    ReverbSlot();

    void prepare(double sampleRate, int blockSize) override;
    void process(juce::AudioBuffer<float>&) override;

    juce::String getName()     override { return "Reverb"; }
    juce::String getCategory() override { return "Send"; }
    juce::Component* createEditor() override { return nullptr; }
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    bool isEnabled() const     { return enabled; }
    void setEnabled(bool e)    { enabled = e; }
    void setLevel(float v)     { level  = juce::jlimit(0.0f, 1.0f, v); }

    // Send-bus processing: overwrites buffer with wet-only reverb output.
    void processReturn(juce::AudioBuffer<float>&);

    void setAlgorithm(int index);
    int  getAlgorithmIndex() const { return algorithmIndex; }

    void setParam(const juce::String& id, float value);

    static std::vector<FXAlgorithmDef> allDefs() { return FXAlgorithmRegistry::reverbAlgorithms(); }

private:
    void applyAlgorithmPreset();
    void updateReverb();

    bool  enabled        = true;
    float level          = 1.0f;
    int   algorithmIndex = 0;

    float size      = 0.5f;
    float preDelay  = 10.0f;
    float diffusion = 0.7f;
    float damp      = 0.4f;
    float mod       = 0.2f;
    float dirt      = 0.0f;

    double sr = 44100.0;

    juce::Reverb reverb;
    juce::Reverb::Parameters reverbParams;

    // Pre-delay buffer
    static constexpr int MaxPreDelaySamples = 192000 / 10;  // ~100ms at 192kHz
    std::vector<float> preDelayBufL, preDelayBufR;
    int preDelayWrite = 0;
};
