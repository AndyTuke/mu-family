#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// The smallest processor that can own an APVTS, for tests that bind a layer to real parameters.
struct ParamHost : juce::AudioProcessor
{
    juce::AudioProcessorValueTreeState apvts;
    explicit ParamHost(juce::AudioProcessorValueTreeState::ParameterLayout l)
        : juce::AudioProcessor(BusesProperties().withOutput("Out", juce::AudioChannelSet::stereo())), apvts(*this, nullptr, "T", std::move(l)) {}
    const juce::String getName() const override { return "T"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
};
