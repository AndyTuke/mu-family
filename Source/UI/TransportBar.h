#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include "Components/NudgeInput.h"
#include "Components/MuClidLookAndFeel.h"

class TransportBar : public juce::Component, private juce::Timer
{
public:
    explicit TransportBar(PluginProcessor& proc);
    ~TransportBar() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    PluginProcessor& proc;
    const bool isStandalone;

    juce::TextButton playBtn;
    NudgeInput       bpmInput { "BPM", 20, 300, 120 };

    void timerCallback() override;
    void refreshPlayBtn();
};
