#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Components/MuClidLookAndFeel.h"

// Concentric ring display showing euclidean hit patterns.
// Ring A (purple) is outermost, Ring B (coral) second, Ring C (amber, dashed) innermost.
// Timer runs at 30 Hz for pulse animation. Call pulseA/pulseB on each sequencer hit.
class RhythmCircle : public juce::Component, public juce::Timer
{
public:
    RhythmCircle();
    ~RhythmCircle() override;

    void setPatterns(const std::vector<bool>& patA,
                     const std::vector<bool>& patB,
                     const std::vector<bool>& patC = {});
    void setCurrentSteps(int stepA, int stepB);
    void pulseA();
    void pulseB();

    void paint(juce::Graphics&) override;
    void timerCallback() override;

private:
    std::vector<bool> patternA, patternB, patternC;
    int currentStepA = 0, currentStepB = 0;
    float pulseAlphaA = 0.0f, pulseAlphaB = 0.0f;

    void drawRing(juce::Graphics& g,
                  const std::vector<bool>& pattern,
                  float cx, float cy,
                  float outerR, float innerR,
                  juce::Colour hitColour,
                  int currentStep,
                  float pulseAlpha,
                  bool dashed = false);
};
