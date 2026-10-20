#pragma once

#include "Plugin/ProcessorBase.h"        // mu-core base
#include "Plugin/MixerFxParams.h"        // mu-core: shared global-FX / mixer APVTS layout
#include "Persistence/LayerTypes.h"      // the layer types this build offers
#include "Persistence/TronicState.h"     // state names, slot prefixes
#include "Sequencer/EmptyLayer.h"

#include <array>
#include <atomic>
#include <memory>

// mu-Tronic — a performance unit of 16 layer slots (Standalone only, freeware).
//
// The ProcessorBase subclass owning the shared platform (the APVTS mixer / FX layout, the MixerEngine and FXChain)
// and sixteen slots, each holding one layer. A slot starts Empty and takes a layer of any type the build
// registers (Persistence/LayerTypes.cpp); the slot's type changes with ProcessorBase::setLayerType, in two view
// publications with nothing muted. The audio thread reaches layers only through the published view
// (LayerAccess::Published): this file never reads layers[] on the audio thread. The count is the capacity, 16,
// for the processor's lifetime; "delete" empties a slot and nothing shifts.
namespace mu_tronic
{

class PluginProcessor : public ProcessorBase
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

    const juce::String getName() const override { return juce::String (juce::CharPointer_UTF8 ("\xce\xbc-Tronic")); }
    juce::String getProductSlug() const override { return "mu-tronic"; }
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

    // ── Slots ────────────────────────────────────────────────────────────────
    // Sixteen fixed slots; a deleted layer leaves its slot Empty and nothing shifts. Reordering is not offered
    // yet (swapLayers needs the Published-mode quiesce).
    bool layersAreSlots() const override { return true; }
    bool canEditLayers() const override { return true; }
    bool canReorderLayers() const override { return false; }

    // The slot's name: the one the user typed, else "Slot 5" while it is Empty and "<Type> 5" once it holds a layer.
    juce::String getLayerName(int idx) const override;
    // Message thread. Renames the slot's layer; empty text returns it to the default name.
    void setLayerName(int idx, const juce::String& name);
    // The palette colour is allocated per slot and stays with it.
    int getLayerColourIndex(int idx) const override
    {
        const auto* layer = getLayer(idx);
        return layer != nullptr ? layer->colourIndex : 0;
    }
    // The type of slot `idx`'s layer (Empty for a slot out of range).
    mu_core::LayerTypeId getLayerTypeId(int idx) const;

    // ── Presets ───────────────────────────────────────────────────────────────
    // Full = .muTronic (the whole state, wrapped); a layer preset belongs to its type's own product and folder
    // (mu-Toni's Arps folder for Arp), found from the registry. The folder and extension answer for the selected
    // slot's type (empty while that slot is Empty); the shown layer is the selected slot (ProcessorBase::setSelectedSlot).
    juce::File   getLayerPresetDir()       const override;
    juce::String getLayerPresetExtension() const override;
    juce::String getFullPresetExtension()  const override { return kFullPresetExtension; }
    juce::Array<juce::File> slotPresetFiles(int slot) const override;   // the slot type's folder; an Empty slot lists every type's

    const char*     getFullPresetTag() const override { return kFullPresetTag; }
    juce::ValueTree captureFullPreset() override { return captureState(); }
    void            useLoadedFullPreset(juce::ValueTree state) override;

    // The state tree of a host session or a full preset (message thread).
    juce::ValueTree captureState();
    // Applies a composed state: each slot's type first (a changed type goes through setLayerType, so nothing is
    // muted), then the rows and extras, then the mixer / FX engines are re-seeded.
    void applyStateTree(const juce::ValueTree& state);

protected:
    void applyFullMidiPreset(const juce::File& f) override
    {
        loadPreset(f);
        if (! layerSwapper.hasFullPending() && onPresetSwapCommitted) onPresetSwapCommitted();
    }

    // A fresh Empty layer for slot `slot` (every other type comes from the registry).
    std::unique_ptr<Layer> createLayer(int slot) override;

    // The non-empty slots: what the CPU meter and a controller count.
    int getNumActiveLayers() const override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void setupLayerState();
    juce::ValueTree toLayerState(const juce::ValueTree& tree) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace mu_tronic
