#pragma once

#include "Plugin/ProcessorBase.h"            // mu-core base
#include "Audio/SynthVoice.h"                // mu-tant voice
#include "Audio/WavetableBank.h"

// mu-tant — wavetable drone synth. FIRST STAB: a single free-running layer
// (Osc1 + Osc2 -> cross-mod -> mix -> mu-core filter), driven entirely by APVTS
// params with a GenericAudioProcessorEditor so the engine can be heard + tweaked
// before the real UI, the 8-layer expansion, the drawable gate, and the mixer/FX
// integration land.
namespace mu_tant
{

class PluginProcessor : public ProcessorBase
{
public:
    PluginProcessor();
    ~PluginProcessor() override = default;

    // ── AudioProcessor ───────────────────────────────────────────────────────
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "mu-Tant"; }
    bool acceptsMidi()  const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ── ProcessorBase channel metadata (single layer for now) ─────────────────
    int          getNumChannels()              const override { return 1; }
    juce::String getChannelName(int)           const override { return "Layer 1"; }
    int          getChannelColourIndex(int)    const override { return 0; }

    // ── ProcessorBase preset wiring (per design-voice.md file formats) ────────
    juce::File   getPerSlotPresetDir()       const override { return {}; }   // TODO: content dir
    juce::String getPerSlotPresetExtension() const override { return "muPattern"; }
    juce::File   getFullPresetDir()          const override { return {}; }   // TODO: content dir
    juce::String getFullPresetExtension()    const override { return "muTant"; }

protected:
    // MIDI program-change apply hooks — stubbed in the first stab (no preset I/O yet).
    void applyMidiPresetSlot(int, const juce::File&) override {}
    void applyFullMidiPreset(const juce::File&)      override {}

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    VoiceConfig readConfig() const;

    WavetableBank bank;
    VoiceEngine   voice;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace mu_tant
