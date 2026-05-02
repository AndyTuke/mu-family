#pragma once

#include "EffectAlgorithmBase.h"

class LadderFilterEffect : public EffectAlgorithmBase
{
public:
    LadderFilterEffect()
    {
        def = FXAlgorithmRegistry::effectAlgorithms()[4];
    }

    const FXAlgorithmDef& getDef() const override { return def; }

    void prepareInner(double sampleRate, int blockSize) override
    {
        juce::dsp::ProcessSpec spec{ sampleRate, (juce::uint32)blockSize, 2 };
        filter.prepare(spec);
        updateFilter();
    }

    void processInner(juce::dsp::AudioBlock<float>& block) override
    {
        filter.process(juce::dsp::ProcessContextReplacing<float>(block));
    }

    void setParam(const juce::String& id, float value) override
    {
        if      (id == "cutoff")    { cutoff    = value; filter.setCutoffFrequencyHz(cutoff); }
        else if (id == "resonance") { resonance = value / 100.0f; filter.setResonance(resonance); }
        else if (id == "drive")     { drive     = value / 100.0f; filter.setDrive(1.0f + drive * 3.0f); }
        else if (id == "mode")
        {
            mode = static_cast<int>(value);
            using Mode = juce::dsp::LadderFilterMode;
            switch (mode)
            {
                case 0: filter.setMode(Mode::LPF24); break;
                case 1: filter.setMode(Mode::HPF24); break;
                case 2: filter.setMode(Mode::BPF24); break;
                default: filter.setMode(Mode::LPF24); break;
            }
        }
    }

private:
    void updateFilter()
    {
        filter.setCutoffFrequencyHz(cutoff);
        filter.setResonance(resonance);
        filter.setDrive(1.0f + drive * 3.0f);
        filter.setMode(juce::dsp::LadderFilterMode::LPF24);
    }

    FXAlgorithmDef def;

    float cutoff    = 2000.0f;
    float resonance = 0.3f;
    float drive     = 0.0f;  // 0 = no drive (setDrive receives 1.0 = clean)
    int   mode      = 0;

    juce::dsp::LadderFilter<float> filter;
};
