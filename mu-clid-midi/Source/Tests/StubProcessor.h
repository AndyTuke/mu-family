#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Plugin/ParameterLayout.h"

namespace mu_clid_midi::tests {

// A bare AudioProcessor that owns the product's real parameter layout, so tests can exercise the
// parameters and the session state without the full plugin (editor, transport, sequencer).
struct StubProcessor : juce::AudioProcessor
{
    StubProcessor()
        : juce::AudioProcessor(BusesProperties()),
          apvts(*this, nullptr, juce::Identifier("MuClidState"), createParameterLayout())
    {}

    const juce::String getName() const override { return "stub"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    // The actual (de-normalised) value of a parameter.
    float actual(const juce::String& id)
    {
        auto* p = apvts.getParameter(id);
        return p != nullptr ? p->convertFrom0to1(p->getValue()) : -9999.0f;
    }
    void setActual(const juce::String& id, float v)
    {
        if (auto* p = apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(v));
    }

    juce::AudioProcessorValueTreeState apvts;
};

} // namespace mu_clid_midi::tests
