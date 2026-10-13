#pragma once

#include "Plugin/ProcessorBase.h"

// A minimal ProcessorBase for UI tests: no parameters, no layers, a stub of every pure virtual.
// The wrapper type is settable so tests can build the standalone and plugin variants of a page.
struct TestProcessorBase : ProcessorBase
{
    explicit TestProcessorBase(WrapperType wrapper)
        : ProcessorBase(BusesProperties().withOutput("Out", juce::AudioChannelSet::stereo()),
                        layoutFor(wrapper)) {}

    // With a parameter layout, for tests that drive a parameter through the control map.
    TestProcessorBase(WrapperType wrapper, juce::AudioProcessorValueTreeState::ParameterLayout layout)
        : ProcessorBase(BusesProperties().withOutput("Out", juce::AudioChannelSet::stereo()),
                        (setTypeOfNextNewPlugin(wrapper), std::move(layout))) {}

    // wrapperType is const and read from a thread-local at construction, so set it first.
    static juce::AudioProcessorValueTreeState::ParameterLayout layoutFor(WrapperType wrapper)
    {
        setTypeOfNextNewPlugin(wrapper);
        return {};
    }

    int getNumLayers() const override { return 0; }
    Layer* getLayer(int) override { return nullptr; }
    juce::String getLayerName(int) const override { return {}; }
    int getLayerColourIndex(int) const override { return 0; }

    juce::File   getLayerPresetDir()       const override { return {}; }
    juce::String getLayerPresetExtension() const override { return ".muTest"; }
    juce::String getFullPresetExtension()  const override { return ".muTest"; }
    void applyFullMidiPreset(const juce::File&) override {}

    const juce::String getName() const override          { return "base-test"; }
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
