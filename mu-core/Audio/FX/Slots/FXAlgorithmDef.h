#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
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
    // The value that sits mid-knob (0 = linear): a time or rate range is skewed so the short end, where it is played,
    // gets the travel, as mu-Clid's envelope times and cutoff do. Stored 0..1 knob positions map through this.
    float centre = 0.0f;

    bool  isSkewed() const noexcept { return centre > minVal && centre < maxVal; }
    // JUCE's skew-for-centre: position 0.5 lands on `centre`.
    float skew() const noexcept     { return isSkewed() ? std::log(0.5f) / std::log((centre - minVal) / (maxVal - minVal)) : 1.0f; }
    // The actual value for a knob position 0..1, and the position of an actual value.
    float actualFromNorm(float n) const noexcept
    {
        n = std::clamp(n, 0.0f, 1.0f);
        return minVal + (maxVal - minVal) * (isSkewed() ? std::pow(n, 1.0f / skew()) : n);
    }
    float normFromActual(float v) const noexcept
    {
        if (maxVal <= minVal) return 0.0f;
        const float p = std::clamp((v - minVal) / (maxVal - minVal), 0.0f, 1.0f);
        return isSkewed() ? std::pow(p, skew()) : p;
    }
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
        // Distortion algorithms moved to per-channel Drive stage in the voice chain.
        static const std::vector<FXAlgorithmDef> s_defs = {
            { "chorus", "Chorus", "Modulation", {
                { "rate",   "Rate",   0.1f, 8.0f, 1.0f, "Hz", 1.0f },
                { "depth",  "Depth",  0.0f, 100.0f, 50.0f, "%" },
                { "voices", "Voices", 2.0f, 4.0f, 2.0f, "" },
                { "spread", "Spread", 0.0f, 100.0f, 50.0f, "%" },
                { "mix",    "Mix",    0.0f, 100.0f, 50.0f, "%" },
            }, 1 },
            { "flanger", "Flanger", "Modulation", {
                { "rate",     "Rate",     0.01f, 20.0f, 0.5f, "Hz", 0.5f },
                { "depth",    "Depth",    0.0f, 100.0f, 50.0f, "%" },
                { "feedback", "Feedback", -100.0f, 100.0f, 0.0f, "%" },
                { "mix",      "Mix",      0.0f, 100.0f, 50.0f, "%" },
            }, 1 },
            { "phaser", "Phaser", "Modulation", {
                { "rate",     "Rate",     0.1f, 8.0f, 0.5f, "Hz", 1.0f },
                { "depth",    "Depth",    0.0f, 100.0f, 50.0f, "%" },
                { "stages",   "Stages",   2.0f, 12.0f, 6.0f, "" },
                { "feedback", "Feedback", 0.0f, 100.0f, 50.0f, "%" },
                { "mix",      "Mix",      0.0f, 100.0f, 50.0f, "%" },
            }, 1 },
            { "echo", "Echo", "Time", {
                { "time",     "Time",     1.0f, 500.0f, 250.0f, "ms", 80.0f },
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
                { "decay",     "Decay",     0.1f, 10.0f, 2.5f, "s", 1.5f },
                { "predelay",  "Pre-delay", 0.0f, 100.0f, 10.0f, "ms", 20.0f },
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
