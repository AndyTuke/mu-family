#pragma once

#include "Plugin/ProcessorBase.h"        // mu-core base
#include "Persistence/PhraseParams.h"    // the Phrase type's parameter table (ppi, addLayerParams)
#include "Plugin/MixerFxParams.h"        // mu-core: shared global-FX / mixer APVTS layout
#include "Sequencer/Phrase.h"            // the Phrase layer
#include "Plugin/ProcessorLayerHostView.h"   // mu-core: the layer host view over this processor
#include "Sequencer/PhraseSlotHost.h"      // the Phrase layers' host adapter: loop loading, and what the UI reads

#include <array>
#include <atomic>
#include <memory>

// mu-Tilate: loop slicer. The ProcessorBase subclass owning the shared platform (the APVTS mixer/FX layout, the
// MixerEngine + FXChain, 1..8 Phrase layers for the shared sidebar and MixerOverlay). Each layer is a Phrase: one loop, its
// slices, one monophonic player and one timeline row, rendered through the shared layer contract.
namespace mu_tilate
{

class PluginProcessor : public ProcessorBase,
                        private mu_core::ProcessorLayerHostView,   // before PhraseSlotHost: its host view
                        public PhraseSlotHost
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    // ── AudioProcessor ───────────────────────────────────────────────────────
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return juce::String(juce::CharPointer_UTF8("\xce\xbc-Tilate")); }
    juce::String getProductSlug() const override { return "mu-tilate"; }   // key in latest.json
    bool acceptsMidi()  const override { return true; }    // program changes and the standalone's MIDI clock
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

    // ── ProcessorBase layer metadata (drives sidebar + mixer) ─────────────────
    juce::String getLayerName(int idx) const override
    {
        return (idx >= 0 && idx < mu_limits::kMaxLayers) ? "Layer " + juce::String(idx + 1) : juce::String();
    }
    int getLayerColourIndex(int idx) const override
    {
        return (idx >= 0 && idx < mu_limits::kMaxLayers) ? layers[(size_t) idx]->colourIndex : 0;
    }
    bool canEditLayers() const override { return true; }
    // The audio state of a slot stays put while its Phrase moves, so it is cleared for the new tenant (called under
    // layersLock; allocation-free).
    void layersMoved(int firstIndex, int lastIndex) override;

    // ── Presets: full = .muTilate, per-layer = .muPhrase ──────────────────────
    juce::File   getLayerPresetDir()       const override;
    juce::String getLayerPresetExtension() const override { return "muPhrase"; }
    juce::String getFullPresetExtension()    const override { return "muTilate"; }
    const char*     getFullPresetTag() const override { return "MuTilatePreset"; }
    juce::ValueTree captureFullPreset() override { return captureState(); }
    void            useLoadedFullPreset(juce::ValueTree state) override;
    void saveLayerPreset(int layer, const juce::String& name) override;
    void loadLayerPreset(int layer, const juce::File& file) override;
    juce::ValueTree captureLayerPresetTree(int layer) override;



protected:
    void stageLayerPresetTree(int layer, juce::ValueTree tree, bool keepIdentity) override;
    void applyFullMidiPreset(const juce::File& f) override
    {
        loadPreset(f);
        if (! layerSwapper.hasFullPending() && onPresetSwapCommitted) onPresetSwapCommitted();
    }
    // A default Phrase for slot `slot`, built and prepared before a structural edit takes the lock.
    std::unique_ptr<Layer> createLayer(int slot) override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // State shared by the session save/restore and full presets (PluginProcessor_Preset.cpp).
    void            setupLayerState();
    juce::ValueTree captureState() { return captureComposedState(); }
    juce::ValueTree toLayerState(const juce::ValueTree& tree) const;
    void            applyStateTree(const juce::ValueTree& state);
    void            applyLayerTree(int layer, const juce::ValueTree& tree);

    std::atomic<double> currentSampleRate { 44100.0 };
    std::atomic<int>    currentBlockSize  { 512 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace mu_tilate
