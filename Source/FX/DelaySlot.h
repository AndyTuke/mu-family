#pragma once

#include "FXSlotBase.h"
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

    bool isEnabled() const  { return enabled; }
    void setEnabled(bool e) { enabled = e; }
    void setSend(float v)   { sendAmount = juce::jlimit(0.0f, 1.0f, v); }

    // Send-bus processing: runs delay with no dry/wet blend (wet-only output).
    void processReturn(juce::AudioBuffer<float>&);

    // Free mode
    void setDelayMs(float ms);

    // Sync mode: denominator of note value (1=whole, 2=half, 4=quarter, 8=eighth, etc.)
    // dotted and triplet modify the time.  count multiplies the resulting duration.
    void setTimeDivision(int denominator, bool dotted, bool triplet);
    void setTimeMode(TimeMode m) { timeMode = m; updateDelayFromMode(); }
    void setTimeCount(int count);
    TimeMode getTimeMode() const { return timeMode; }

    void setFeedback(float fb)  { feedback = juce::jlimit(0.0f, 0.98f, fb); }
    void setSpread(float s)     { spread   = juce::jlimit(0.0f, 1.0f, s); }
    void setDirt(float d)       { dirt     = juce::jlimit(0.0f, 1.0f, d); }
    void setHostBpm(double bpm) { hostBpm  = bpm; if (timeMode == TimeMode::Sync) updateDelayFromMode(); }

private:
    void updateDelayFromMode();
    void setDelaySamplesLR(float sampL, float sampR);
    float processDirt(float x) const;

    static constexpr int MaxDelaySamples = 4 * 192000;  // 4s at 192kHz

    bool   enabled    = true;
    float  sendAmount = 1.0f;
    float  feedback   = 0.45f;
    float  spread     = 0.0f;
    float  dirt       = 0.0f;
    double sr         = 44100.0;
    double hostBpm    = 120.0;

    TimeMode timeMode = TimeMode::Free;
    float    delayMs  = 250.0f;
    int      syncDenominator = 4;
    bool     syncDotted      = false;
    bool     syncTriplet     = false;
    int      syncCount        = 1;

    float targetDelayL = 0.0f;   // in samples
    float targetDelayR = 0.0f;

    std::vector<float> bufL, bufR;
    int writePosL = 0, writePosR = 0;
    float feedL = 0.0f, feedR = 0.0f;  // feedback state

    juce::AudioBuffer<float> dryBuffer;
};
