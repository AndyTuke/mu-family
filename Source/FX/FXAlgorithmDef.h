#pragma once

#include <juce_core/juce_core.h>
#include <vector>

// Describes one parameter within an FX algorithm.
struct FXParamDef
{
    juce::String id;
    juce::String name;
    float minVal;
    float maxVal;
    float defaultVal;
    juce::String units;
};

// Metadata for one FX algorithm. Drives both UI layout and DSP routing.
// visibleWhen conditions are evaluated at runtime by FXRow.
struct FXAlgorithmDef
{
    juce::String id;
    juce::String name;
    juce::String category;       // "Distortion", "Filter", "Modulation"
    std::vector<FXParamDef> params;
    int oversamplingFactor = 1;  // 1 = none, 2 = 2x, 4 = 4x
};

// Registry of all EffectSlot algorithms, in display order.
struct FXAlgorithmRegistry
{
    static std::vector<FXAlgorithmDef> effectAlgorithms()
    {
        return {
            { "soft_clip", "Soft Clip", "Distortion", {
                { "drive",  "Drive",  0.0f, 100.0f, 50.0f, "%" },
                { "output", "Output", -24.0f, 0.0f, 0.0f, "dB" },
                { "tone",   "Tone",   20.0f, 20000.0f, 8000.0f, "Hz" },
            }, 1 },
            { "hard_clip", "Hard Clip", "Distortion", {
                { "drive",     "Drive",     0.0f, 100.0f, 50.0f, "%" },
                { "threshold", "Threshold", -24.0f, 0.0f, -6.0f, "dB" },
                { "output",    "Output",    -24.0f, 0.0f, -12.0f, "dB" },
                { "tone",      "Tone",      20.0f, 20000.0f, 8000.0f, "Hz" },
            }, 4 },
            { "foldback", "Foldback", "Distortion", {
                { "drive",  "Drive",  0.0f, 100.0f, 50.0f, "%" },
                { "folds",  "Folds",  1.0f, 8.0f, 2.0f, "" },
                { "output", "Output", -24.0f, 0.0f, 0.0f, "dB" },
                { "tone",   "Tone",   20.0f, 20000.0f, 8000.0f, "Hz" },
            }, 4 },
            { "bitcrush", "Bitcrush", "Distortion", {
                { "bits",   "Bits",   2.0f, 16.0f, 16.0f, "" },
                { "rate",   "Rate",   0.0f, 100.0f, 0.0f, "%" },
                { "output", "Output", -24.0f, 0.0f, 0.0f, "dB" },
                { "tone",   "Tone",   20.0f, 20000.0f, 8000.0f, "Hz" },
            }, 2 },
            { "ladder_filter", "Ladder Filter", "Filter", {
                { "cutoff",    "Cutoff",    20.0f, 20000.0f, 2000.0f, "Hz" },
                { "resonance", "Resonance", 0.0f, 100.0f, 30.0f, "%" },
                { "drive",     "Drive",     0.0f, 100.0f, 0.0f, "%" },
                { "mode",      "Mode",      0.0f, 2.0f, 0.0f, "" },
            }, 2 },
            { "chorus", "Chorus", "Modulation", {
                { "rate",   "Rate",   0.1f, 8.0f, 1.0f, "Hz" },
                { "depth",  "Depth",  0.0f, 100.0f, 50.0f, "%" },
                { "voices", "Voices", 2.0f, 4.0f, 2.0f, "" },
                { "spread", "Spread", 0.0f, 100.0f, 50.0f, "%" },
                { "mix",    "Mix",    0.0f, 100.0f, 50.0f, "%" },
            }, 1 },
            { "phaser", "Phaser", "Modulation", {
                { "rate",     "Rate",     0.1f, 8.0f, 0.5f, "Hz" },
                { "depth",    "Depth",    0.0f, 100.0f, 50.0f, "%" },
                { "stages",   "Stages",   2.0f, 12.0f, 6.0f, "" },
                { "feedback", "Feedback", 0.0f, 100.0f, 50.0f, "%" },
                { "mix",      "Mix",      0.0f, 100.0f, 50.0f, "%" },
            }, 1 },
            { "comb_filter", "Comb Filter", "Filter", {
                { "freq",     "Frequency", 50.0f, 8000.0f, 500.0f, "Hz" },
                { "feedback", "Feedback",  0.0f, 100.0f, 50.0f, "%" },
                { "output",   "Output",    -24.0f, 0.0f, 0.0f, "dB" },
                { "mix",      "Mix",       0.0f, 100.0f, 50.0f, "%" },
            }, 1 },
        };
    }

    static std::vector<FXAlgorithmDef> reverbAlgorithms()
    {
        std::vector<FXParamDef> commonParams = {
            { "size",      "Size",      0.0f, 1.0f, 0.5f, "" },
            { "predelay",  "Pre-delay", 0.0f, 100.0f, 10.0f, "ms" },
            { "diffusion", "Diffusion", 0.0f, 1.0f, 0.7f, "" },
            { "damp",      "Damp",      0.0f, 1.0f, 0.4f, "" },
            { "mod",       "Mod",       0.0f, 1.0f, 0.2f, "" },
            { "dirt",      "Dirt",      0.0f, 1.0f, 0.0f, "" },
        };
        return {
            { "room",   "Room",   "Reverb", commonParams, 1 },
            { "hall",   "Hall",   "Reverb", commonParams, 1 },
            { "plate",  "Plate",  "Reverb", commonParams, 1 },
            { "spring", "Spring", "Reverb", commonParams, 1 },
        };
    }
};
