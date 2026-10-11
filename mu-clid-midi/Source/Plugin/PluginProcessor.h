#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Plugin/ProcessorBase.h"
#include "Audio/MidiOutputEngine.h"
#include "Sequencer/SequencerEngine.h"    // mu-Clid's, shared until Rhythm / SequencerEngine move to mu-core
#include "Sequencer/RhythmPlayState.h"    // mu-Clid's, shared with RhythmCircle

#include <atomic>

namespace mu_clid_midi {

using mu_clid::Rhythm;
using mu_clid::RhythmPlayState;
using mu_clid::SequencerEngine;

// mu-Clid MIDI: one Euclidean rhythm that plays MIDI notes on the host transport. No audio, no
// samples, no presets, no licensing; it shares Rhythm and the Euclid parameter set with mu-Clid.
class PluginProcessor : public ProcessorBase,
                        private juce::AudioProcessorValueTreeState::Listener
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }
    // A MIDI effect makes no audio, so there is no tail for the host to keep rendering.
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // The one rhythm. EuclideanPanel reads it through the ProcessorBase layer accessors.
    Rhythm&       getRhythm()       { return sequencer.getRhythm(0); }
    const Rhythm& getRhythm() const { return sequencer.getRhythm(0); }
    int          getNumChannels() const override { return 1; }
    Layer*       getLayer(int i) override { return i == 0 ? &getRhythm() : nullptr; }
    using ProcessorBase::getLayer;
    int          getMaxChannels() const override { return 1; }
    juce::String getChannelName(int idx) const override { return idx == 0 ? juce::String(getRhythm().name) : juce::String(); }
    int          getChannelColourIndex(int idx) const override { return idx == 0 ? getRhythm().colourIndex : 0; }

    // Master loop: the shared MasterLoopSection reads these.
    int getMasterLoopSteps() const override       { return sequencer.getMasterLoopSteps(); }
    int getMasterLoopCurrentStep() const override { return sequencer.getMasterLoopCurrentStep(); }

    // A rhythm preset is the one thing mu-Clid MIDI shares with mu-Clid (.muRhythm); this product
    // has no preset browser yet, so these only satisfy the shared base.
    juce::File   getLayerPresetDir() const override       { return getContentDir().getChildFile("Rhythms"); }
    juce::String getLayerPresetExtension() const override { return "muRhythm"; }
    // Empty on purpose: this product has no full-preset format (its state is the host session and
    // the .muRhythm layer preset), and the preset controls are hidden, so nothing reads this.
    juce::String getFullPresetExtension() const override  { return {}; }

    // UI play state: written by the audio thread, read by the editor at 30 Hz.
    RhythmPlayState     rhythmPlayState;
    std::atomic<float>  beatFraction     { 0.0f };   // fractional position within the current 1/16 step
    std::atomic<bool>   sequencerPlaying { false };
    std::atomic<double> lastBeatPos      { 0.0 };    // most recent beat position (for the UI playhead)

    // True while a state restore or a bulk parameter push is running, so editor listeners skip
    // their per-parameter refresh.
    bool isApvtsLoading() const noexcept { return apvtsLoading.load(std::memory_order_acquire); }

protected:
    // This product never loads full presets, so a MIDI program change has nothing to do.
    void applyFullMidiPreset(const juce::File&) override {}

private:
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void syncRhythmParam(const juce::String& suffix, float value);
    void pushRhythmToApvts();
    void syncRhythmFromApvts();

    // Reads the host transport and publishes it for the UI.
    struct BlockTransport { bool playing; double beatPos; };
    BlockTransport deriveTransport(int numSamples);
    // Steps the sequencer, fires the MIDI note and publishes the UI play state; flushes the note-offs.
    void advanceSequencer(bool playing, double beatPos, juce::MidiBuffer& midi, int numSamples);

    SequencerEngine sequencer;
    MidiOutputEngine midiEngine;

    // suspendProcessing() does not wait for a running block, so the message thread takes this
    // around any edit to the rhythm and the audio thread try-locks it.
    juce::CriticalSection rhythmsLock;

    // Raised for the whole of a bulk push or restore so parameterChanged skips the pattern rebuild.
    std::atomic<bool> apvtsLoading { false };

    // Cached parameter pointers, so processBlock pays no per-block id lookup.
    std::atomic<float>* midiNotePtr  = nullptr;
    std::atomic<float>* accentAmtPtr = nullptr;

    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace mu_clid_midi
