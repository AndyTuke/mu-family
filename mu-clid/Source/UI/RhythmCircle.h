#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/MuLookAndFeel.h"
#include "Sequencer/HitGenerator.h"
#include "Plugin/PluginProcessor.h"
#include <array>

// Concentric ring display showing euclidean hit patterns.
// Ring A (purple) is outermost, Ring B (coral) second, Ring C (amber, dashed) innermost.
// During playback, all rings rotate so the current step sits at 12 o'clock.
// Hit pulses: expanding arc radiates outward from the hit arc + centre hub.
class RhythmCircle : public juce::Component, public juce::Timer
{
public:
    RhythmCircle();
    ~RhythmCircle() override;

    void setPatterns(const std::vector<StepType>& patA,
                     const std::vector<StepType>& patB,
                     const std::vector<StepType>& patC = {});

    // Connect to PluginProcessor play-state atomics for self-driven animation.
    // state is non-const so we can update lastHitCount tracking (Issue #43).
    void setPlayState(PluginProcessor::RhythmPlayState*  state,
                      const std::atomic<float>*          beatFrac,
                      const std::atomic<bool>*            playing,
                      juce::Colour                         colour);

    void paint(juce::Graphics&) override;
    void timerCallback() override;

private:
    std::vector<StepType> patternA, patternB, patternC;

    // Non-owning pointers to PluginProcessor atomics
    PluginProcessor::RhythmPlayState*       playState     = nullptr;
    const std::atomic<float>*              beatFracAtom  = nullptr;
    const std::atomic<bool>*               isPlayingAtom = nullptr;
    juce::Colour                            rhythmColour;

    // Per-ring rotation (each ring has its own step count so its own speed)
    float rotAngleA = 0.0f, rotAngleB = 0.0f, rotAngleC = 0.0f;
    float snapFromA = 0.0f, snapFromB = 0.0f, snapFromC = 0.0f;
    float snapProgress = 1.0f;  // 1.0 = snap complete; shared since all rings stop together
    bool  wasPlaying   = false;

    // Expanding arc pulse pool
    struct ArcPulse
    {
        float stepFrac  = 0.0f;  // normalised position in combined pattern [0,1)
        float arcWidth  = 0.0f;  // angular width of the source step
        float alpha     = 0.0f;
        float expand    = 0.0f;  // 0→1 expansion progress
        bool  active    = false;
    };
    static constexpr int kMaxPulses = 4;
    std::array<ArcPulse, kMaxPulses> arcPulses;
    int   nextPulse    = 0;
    float hubAlpha     = 0.0f;
    int   lastHitCount = 0;  // Issue #43: edge-detect against playState->hitCount

    void triggerHitPulse(int combinedStep, int stepsA);

    // per-ring unrotated-Path cache. paint() built ~192 fresh juce::Path objects per
    // frame (3 rings × up to 64 steps × 2 arcs each). The geometry only changes when the
    // ring's radii or step count changes; per-paint rotation is applied via AffineTransform
    // at fillPath() time, so the cached Paths stay valid across frames.
    struct RingCache
    {
        std::vector<juce::Path> stepPaths;
        float cx = -1.0f, cy = -1.0f;
        float outerR = -1.0f, innerR = -1.0f;
        int   stepCount = -1;

        bool matches(float cx_, float cy_, float outerR_, float innerR_, int N) const noexcept
        {
            return cx == cx_ && cy == cy_ && outerR == outerR_ && innerR == innerR_
                && stepCount == N;
        }
        void rebuild(float cx_, float cy_, float outerR_, float innerR_, int N);
    };
    mutable std::array<RingCache, 3> ringCaches;

    void drawRing(juce::Graphics& g,
                  const std::vector<StepType>& pattern,
                  float cx, float cy,
                  float outerR, float innerR,
                  juce::Colour hitClr,
                  int currentStep,
                  float rotOff,
                  RingCache& cache) const;

    static juce::Colour stepColour(StepType t, juce::Colour hitClr, bool isCurrent);
};
