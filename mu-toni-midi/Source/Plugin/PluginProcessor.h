#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Plugin/ProcessorBase.h"
#include "Sequencer/MidiArpCore.h"

namespace mu_toni_midi
{

// mu-Toni MIDI: mu-Toni's arpeggiator as a MIDI effect. The keys held on its input play the arp's scale / chord notes as MIDI
// on the host transport. No audio, no licensing, no presets of its own; the arp is mu-Toni's (the same code).
// The child node of the saved state that holds the modulators.
inline const juce::Identifier kModulatorNode("MidiArpLayer");

class PluginProcessor : public ProcessorBase
{
    // First member: the editor reads the core through the processor.
    MidiArpCore core;

public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using ProcessorBase::processBlockBypassed;
    // A bypassed MIDI effect still lets a pending note-off out, so the note never hangs.
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;
    juce::String getProductSlug() const override { return "mu-toni-midi"; }   // key in latest.json
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // There are no Layer objects: the arp is the product's one voice, its parameters are the a0_* rows.
    int          getNumLayers() const override { return 0; }
    Layer*       getLayer(int) override { return nullptr; }
    using ProcessorBase::getLayer;
    int          getMaxLayers() const override { return 1; }
    juce::String getLayerName(int) const override { return {}; }
    int          getLayerColourIndex(int) const override { return 0; }

    // The shared base asks for these; this product has no preset browser, so nothing reads them.
    juce::File   getLayerPresetDir() const override       { return getContentDir().getChildFile("Arps"); }
    juce::String getLayerPresetExtension() const override { return "muArp"; }
    juce::String getFullPresetExtension() const override  { return {}; }

    // UI: the live arp (the editor polls these at 30 Hz).
    const MidiArpCore& getCore() const noexcept { return core; }
    MidiArpCore&       getCore() noexcept       { return core; }

protected:
    void applyFullMidiPreset(const juce::File&) override {}

private:
    struct BlockTransport { bool playing; double beatPos; double bpm; };
    BlockTransport deriveTransport(int numSamples);

    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace mu_toni_midi
