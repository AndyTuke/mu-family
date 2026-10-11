#pragma once

#include "Plugin/ProcessorCore.h"

#include <array>

// A minimal ProcessorCore for tests: three layers, a stub of every AudioProcessor pure virtual,
// and a hook to call the protected pollHostTransport().
struct TestProcessorCore : ProcessorCore
{
    TestProcessorCore()
        : ProcessorCore(BusesProperties().withOutput("Out", juce::AudioChannelSet::stereo())) {}

    // Mock DAW playhead; each field maps onto one published transport value.
    struct MockPlayHead : juce::AudioPlayHead
    {
        bool   playing   = false;
        bool   hasPos    = true;    // false simulates a host that returns no position
        bool   hasPpq    = true;
        double ppq       = 0.0;
        int    sigNum    = 4;
        int    sigDen    = 4;
        bool   hasBar    = false;
        double barStart  = 0.0;

        juce::Optional<PositionInfo> getPosition() const override
        {
            if (! hasPos) return {};
            PositionInfo info;
            info.setIsPlaying(playing);
            if (hasPpq) info.setPpqPosition(ppq);
            info.setTimeSignature(TimeSignature { sigNum, sigDen });
            if (hasBar) info.setPpqPositionOfLastBarStart(barStart);
            return info;
        }
    };

    static constexpr int kLayers = 3;
    std::array<Layer, kLayers> layers;

    int getNumChannels() const override { return kLayers; }
    using ProcessorCore::getLayer;   // keep the const overload visible past the override
    Layer* getLayer(int i) override { return (i >= 0 && i < kLayers) ? &layers[(size_t) i] : nullptr; }
    juce::String getChannelName(int i) const override
    {
        const auto* l = getLayer(i);
        return l != nullptr ? juce::String(l->name) : juce::String();
    }
    int getChannelColourIndex(int i) const override
    {
        const auto* l = getLayer(i);
        return l != nullptr ? l->colourIndex : 0;
    }

    mu_core::HostTransport callPoll() { return pollHostTransport(); }

    const juce::String getName() const override          { return "core-test"; }
    void prepareToPlay(double, int) override             {}
    void releaseResources() override                      {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    juce::AudioProcessorEditor* createEditor() override   { return nullptr; }
    bool hasEditor() const override                       { return false; }
    bool acceptsMidi() const override                     { return false; }
    bool producesMidi() const override                    { return false; }
    double getTailLengthSeconds() const override          { return 0.0; }
    int getNumPrograms() override                         { return 1; }
    int getCurrentProgram() override                      { return 0; }
    void setCurrentProgram(int) override                  {}
    const juce::String getProgramName(int) override       { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override   {}
};
