#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Components/MuClidLookAndFeel.h"
#include "../Sequencer/HitGenerator.h"

// Concentric ring display showing euclidean hit patterns.
// Ring A (purple) is outermost, Ring B (coral) second, Ring C (amber, dashed) innermost.
// Each step is coloured by its StepType: hit, pre-pad, post-pad, insert-pad, or empty.
// Timer runs at 30 Hz for pulse animation. Call pulseA/pulseB on each sequencer hit.
class RhythmCircle : public juce::Component, public juce::Timer
{
public:
    RhythmCircle();
    ~RhythmCircle() override;

    void setPatterns(const std::vector<StepType>& patA,
                     const std::vector<StepType>& patB,
                     const std::vector<StepType>& patC = {});
    void setCurrentSteps(int stepA, int stepB);
    void pulseA();
    void pulseB();

    void paint(juce::Graphics&) override;
    void timerCallback() override;

private:
    std::vector<StepType> patternA, patternB, patternC;
    int currentStepA = 0, currentStepB = 0;
    float pulseAlphaA = 0.0f, pulseAlphaB = 0.0f;

    void drawRing(juce::Graphics& g,
                  const std::vector<StepType>& pattern,
                  float cx, float cy,
                  float outerR, float innerR,
                  juce::Colour hitColour,
                  int currentStep,
                  float pulseAlpha,
                  bool dashed = false);

    static juce::Colour stepColour(StepType t, juce::Colour hitColour,
                                   bool isCurrent, float pulseAlpha);
};
