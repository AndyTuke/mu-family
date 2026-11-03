#pragma once

#include "Plugin/ProcessorBase.h"        // mu-core base
#include "Persistence/PhraseParams.h"    // the Phrase type's parameter table (ppi, addLayerParams)
#include "Plugin/MixerFxParams.h"        // mu-core: shared global-FX / mixer APVTS layout
#include "Sequencer/Phrase.h"            // the Phrase layer
#include "Sequencer/PhraseAnalysis.h"    // slice detection
#include "UI/PhraseHost.h"               // what the Phrase layer's UI reads from its host

#include <array>
#include <atomic>
#include <memory>

// mu-Tilate: loop slicer. The ProcessorBase subclass owning the shared platform (the APVTS mixer/FX layout, the
// MixerEngine + FXChain, 1..8 Phrase layers for the shared sidebar and MixerOverlay). Each layer is a Phrase: one loop, its
// slices, one monophonic player and one timeline row, rendered through the shared layer contract.
namespace mu_tilate
{

class PluginProcessor : public ProcessorBase,
                        public PhraseHost
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

    // ── PhraseHost: what PhraseContent reads (message thread) ─────────────────
    juce::AudioProcessorValueTreeState& getPhraseParameters() noexcept override { return apvts; }
    Phrase&       getPhrase(int i) noexcept override { return layerAs<Phrase>(i); }
    const Phrase& getPhrase(int i) const noexcept    { return layerAs<Phrase>(i); }
    float getModLive(int layer, int dest) const noexcept override
    {
        return getPhrase(juce::jlimit(0, mu_limits::kMaxLayers - 1, layer)).getModLive(dest);
    }
    int getPhraseColourIndex(int layer) const override { return getLayerColourIndex(layer); }
    bool   isPhraseTransportPlaying() const noexcept override { return isInternalPlaying(); }
    double getPhraseTransportBeat() const noexcept override   { return getInternalBeatPos(); }
    void loadLoopAsync(int layer, const juce::File& file, bool keepSavedEdits = false) override;
    // The same from an already decoded source (no disk), using the section [regionStart, regionEnd) of it.
    void loadLoopAsync(int layer, const juce::File& file, bool keepSavedEdits,
                       std::shared_ptr<const juce::AudioBuffer<float>> decoded, double decodedRate, int regionStart, int regionEnd);
    void layOutSlices(int layer) override;
    void previewSource(int layer, int kind, int slice) override
    {
        if (layer >= 0 && layer < mu_limits::kMaxLayers)
            getPhrase(layer).startPreview((Phrase::PreviewKind) juce::jlimit(0, 2, kind), slice, getInternalBpm());
    }
    void setPreviewLooping(int layer, bool loop) override { if (layer >= 0 && layer < mu_limits::kMaxLayers) getPhrase(layer).setPreviewLooping(loop); }
    void stopPreview(int layer) override { if (layer >= 0 && layer < mu_limits::kMaxLayers) getPhrase(layer).stopPreview(); }
    void setPlayArea(int layer, int start, int end) override { if (layer >= 0 && layer < mu_limits::kMaxLayers) getPhrase(layer).setPlayArea(start, end); }
    void cropLoop(int layer, int start, int end) override { if (layer >= 0 && layer < mu_limits::kMaxLayers) getPhrase(layer).cropTo(start, end); }
    void setSourceRegion(int layer, int start, int end) override;
    juce::String getLoopStatus(int layer) const override;

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

    // The detection settings from layer `layer`'s parameters (any thread: reads the raw atomics).
    DetectSettings detectSettingsOf(int layer) const;
    void redetectSlices(int layer) override;
    // Loads the loop a layer's preset or session names when the layer does not hold that file's audio (message thread).
    void reloadLoopIfNeeded(int layer);
    // Publishes `slices` with a timeline of the layer's length: empty, or (inOrder) the slices back to back from beat 0.
    double layOutInOrder(int layer, const SliceList& slices, double sampleRate, double loopTempo, bool inOrder);

    std::atomic<double> currentSampleRate { 44100.0 };
    std::atomic<int>    currentBlockSize  { 512 };

    // Loop loading: a generation per layer (a newer request supersedes an older one), the status line, and a flag the
    // worker's completion checks so it never touches a processor that is gone.
    std::array<std::atomic<int>, (size_t) mu_limits::kMaxLayers> loopGeneration {};
    // Detect has its own count, so it neither cancels a loop being loaded nor is cancelled by a load's start.
    std::array<std::atomic<int>, (size_t) mu_limits::kMaxLayers> detectGeneration {};
    std::array<juce::String, (size_t) mu_limits::kMaxLayers>     loopStatus;   // message thread
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace mu_tilate
