#pragma once

#include "FXSlotBase.h"
#include <atomic>
#include <vector>

// Insert-style stereo delay with sync/free time, feedback path saturation (dirt),
// and stereo spread. Same insert blending curve as EffectSlot.
class DelaySlot : public FXSlotBase
{
public:
    enum class TimeMode { Sync, Free };

    DelaySlot();

    void prepare(double sampleRate, int blockSize) override;
    void process(juce::AudioBuffer<float>&) override;

    juce::String getName()     override { return "Delay"; }
    juce::String getCategory() override { return "Insert"; }
    juce::Component* createEditor() override { return nullptr; }
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    bool isEnabled() const  { return enabled.load(std::memory_order_relaxed); }
    void setEnabled(bool e) { enabled.store(e, std::memory_order_relaxed); }

    // Send-bus processing: runs delay with no dry/wet blend (wet-only output).
    void processReturn(juce::AudioBuffer<float>&);

    // Free mode
    void setDelayMs(float ms);

    // Sync mode: denominator of note value (1=whole, 2=half, 4=quarter, 8=eighth, etc.)
    // dotted and triplet modify the time.  count multiplies the resulting duration.
    void setTimeDivision(int denominator, bool dotted, bool triplet);
    void setTimeMode(TimeMode m) { timeMode.store(m, std::memory_order_relaxed); updateDelayFromMode(); }
    void setTimeCount(int count);
    TimeMode getTimeMode() const { return timeMode.load(std::memory_order_relaxed); }

    void setFeedback(float fb)  { feedback.store(juce::jlimit(0.0f, 0.98f, fb), std::memory_order_relaxed); }
    void setSpread(float s)     { spread.store(juce::jlimit(0.0f, 1.0f, s), std::memory_order_relaxed); }
    void setDirt(float d)       { dirt.store(juce::jlimit(0.0f, 1.0f, d), std::memory_order_relaxed); }
    void setHostBpm(double bpm) { hostBpm  = bpm; if (timeMode.load(std::memory_order_relaxed) == TimeMode::Sync) updateDelayFromMode(); }

private:
    void updateDelayFromMode();
    void setDelaySamplesLR(float sampL, float sampR);
    float processDirt(float x) const;

    static constexpr int MaxDelaySamples = 4 * 192000;  // 4s at 192kHz

    // Fields read on the audio thread are atomic — setEnabled / setFeedback /
    // setDirt / setSpread / setTimeMode can be called from parameterChanged on
    // any thread when a DAW automates delay parameters. Mirrors ReverbSlot.
    std::atomic<bool>     enabled  { true };
    std::atomic<float>    feedback { 0.45f };
    std::atomic<float>    spread   { 0.0f };
    std::atomic<float>    dirt     { 0.0f };
    double sr         = 44100.0;
    double hostBpm    = 120.0;

    std::atomic<TimeMode> timeMode { TimeMode::Free };
    float    delayMs  = 250.0f;
    int      syncDenominator = 4;
    bool     syncDotted      = false;
    bool     syncTriplet     = false;
    int      syncCount        = 1;

    float targetDelayL   = 0.0f;  // in samples (target)
    float targetDelayR   = 0.0f;
    float smoothedDelayL = 0.0f;  // exponentially smoothed toward target (50ms glide)
    float smoothedDelayR = 0.0f;
    float smoothCoeff    = 1.0f;  // per-block smoothing coefficient

    std::vector<float> bufL, bufR;
    int writePosL = 0, writePosR = 0;
    float feedL = 0.0f, feedR = 0.0f;  // feedback state
};
