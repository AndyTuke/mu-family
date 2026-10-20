#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <cmath>
#include "Audio/MixerEngine.h"
#include "Audio/FX/Slots/FXChain.h"

// Shared fixture for the group-bus tests (BusRoutingTests, BusSidechainTests, BusInsertTests,
// BusStateResetTests): a real MixerEngine driven through the render hook, so no voice engine is needed.
// Every channel plays a constant (DC) level or a tone you set; the mixer's own gains are then exact
// products (the -6 dB headroom trim, the faders) and a test can state the expected output sample.
// The bus tests run only in a build that has buses (mu_limits::kMaxBuses > 0): mu-core-tests-16 and
// mu-core-tests-bus1; the default mu-core-tests has none and skips them.
namespace bus_test
{
inline constexpr int    kBlock = 256;
inline constexpr double kRate  = 48000.0;
inline constexpr float  kTrim  = MixerEngine::kHeadroomTrim;   // the fixed pre-fader trim every channel gets

struct Rig
{
    MixerEngine mixer;
    FXChain     fx;
    juce::AudioBuffer<float> out { 2, kBlock };
    int numActive;

    // Per-channel source: a constant level (dc) unless `tone` is set, in which case a sine of that amplitude.
    std::array<float, MixerEngine::MaxChannels> dc {};
    std::array<float, MixerEngine::MaxChannels> tone {};
    long long samplePos = 0;
    MixerEngine::RenderChannelFn render;

    explicit Rig(int activeChannels = 4) : numActive(activeChannels)
    {
        mixer.prepare(kRate, kBlock);
        fx.prepare(kRate, kBlock);
        render = [this](int ch, juce::AudioBuffer<float>& buf, int n)
        {
            for (int c = 0; c < buf.getNumChannels(); ++c)
                for (int i = 0; i < n; ++i)
                {
                    const float t = tone[(size_t) ch] * std::sin(0.07f * (float) (samplePos + i) + 0.3f * (float) ch);
                    buf.setSample(c, i, dc[(size_t) ch] + t);
                }
        };
    }

    void run(int blocks = 1, juce::AudioBuffer<float>* direct0 = nullptr)
    {
        for (int b = 0; b < blocks; ++b)
        {
            if (direct0 != nullptr)
            {
                std::array<juce::AudioBuffer<float>*, MixerEngine::MaxChannels> d {};
                d[0] = direct0;
                mixer.processBlock(out, numActive, nullptr, fx, kBlock, &d, nullptr, nullptr, &render);
            }
            else
                mixer.processBlock(out, numActive, nullptr, fx, kBlock, nullptr, nullptr, nullptr, &render);
            samplePos += kBlock;
        }
    }

    // The last output sample (left, right): with constant sources it is the whole block's value.
    float outL() const { return out.getSample(0, kBlock - 1); }
    float outR() const { return out.getSample(1, kBlock - 1); }
    // Largest absolute output sample over the block, both sides.
    float outMax() const { return out.getMagnitude(0, kBlock) ; }

    // Bus-array access by runtime index (a build with fewer inserts than a test touches never indexes past the array).
    MixerEngine::BusInsert& insertAt(int bus, int slot) { return mixer.buses[(size_t) bus].insert[slot]; }
};

// True (and logs why not) when this build has no buses, so a test can return early.
inline bool hasBuses(juce::UnitTest& t, int atLeast = 1)
{
    if (mu_limits::kMaxBuses >= atLeast) return true;
    t.logMessage("skipped: this build has fewer than " + juce::String(atLeast) + " group buses (mu_limits::kMaxBuses = "
                 + juce::String(mu_limits::kMaxBuses) + ")");
    return false;
}
} // namespace bus_test
