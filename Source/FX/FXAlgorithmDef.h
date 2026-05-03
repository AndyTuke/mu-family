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
// Methods return const& to static locals — safe to call at any frequency with no allocation.
struct FXAlgorithmRegistry
{
    static const std::vector<FXAlgorithmDef>& effectAlgorithms()
    {
        // Index 0: Chorus, 1: Flanger, 2: Phaser, 3: Echo
        // Distortion algorithms moved to per-rhythm Drive stage in the voice chain.
        static const std::vector<FXAlgorithmDef> s_defs = {
            { "chorus", "Chorus", "Modulation", {
                { "rate",   "Rate",   0.1f, 8.0f, 1.0f, "Hz" },
                { "depth",  "Depth",  0.0f, 100.0f, 50.0f, "%" },
                { "voices", "Voices", 2.0f, 4.0f, 2.0f, "" },
                { "spread", "Spread", 0.0f, 100.0f, 50.0f, "%" },
                { "mix",    "Mix",    0.0f, 100.0f, 50.0f, "%" },
            }, 1 },
            { "flanger", "Flanger", "Modulation", {
                { "rate",     "Rate",     0.1f, 8.0f, 0.5f, "Hz" },
                { "depth",    "Depth",    0.0f, 100.0f, 50.0f, "%" },
                { "feedback", "Feedback", -100.0f, 100.0f, 0.0f, "%" },
                { "mix",      "Mix",      0.0f, 100.0f, 50.0f, "%" },
            }, 1 },
            { "phaser", "Phaser", "Modulation", {
                { "rate",     "Rate",     0.1f, 8.0f, 0.5f, "Hz" },
                { "depth",    "Depth",    0.0f, 100.0f, 50.0f, "%" },
                { "stages",   "Stages",   2.0f, 12.0f, 6.0f, "" },
                { "feedback", "Feedback", 0.0f, 100.0f, 50.0f, "%" },
                { "mix",      "Mix",      0.0f, 100.0f, 50.0f, "%" },
            }, 1 },
            { "echo", "Echo", "Time", {
                { "time",     "Time",     1.0f, 500.0f, 250.0f, "ms" },
                { "feedback", "Feedback", 0.0f, 100.0f, 30.0f, "%" },
                { "spread",   "Spread",   0.0f, 100.0f, 0.0f, "%" },
                { "mix",      "Mix",      0.0f, 100.0f, 50.0f, "%" },
            }, 1 },
        };
        return s_defs;
    }

    static const std::vector<FXAlgorithmDef>& reverbAlgorithms()
    {
        static const std::vector<FXAlgorithmDef> s_defs = [] {
            const std::vector<FXParamDef> commonParams = {
                { "size",      "Size",      0.0f, 1.0f, 0.5f, "" },
                { "predelay",  "Pre-delay", 0.0f, 100.0f, 10.0f, "ms" },
                { "diffusion", "Diffusion", 0.0f, 1.0f, 0.7f, "" },
                { "damp",      "Damp",      0.0f, 1.0f, 0.4f, "" },
                { "mod",       "Mod",       0.0f, 1.0f, 0.2f, "" },
                { "dirt",      "Dirt",      0.0f, 1.0f, 0.0f, "" },
            };
            return std::vector<FXAlgorithmDef> {
                { "room",   "Room",   "Reverb", commonParams, 1 },
                { "hall",   "Hall",   "Reverb", commonParams, 1 },
                { "plate",  "Plate",  "Reverb", commonParams, 1 },
                { "spring", "Spring", "Reverb", commonParams, 1 },
            };
        }();
        return s_defs;
    }
};
