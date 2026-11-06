#pragma once

#include "Sequencer/LayerHostView.h"
#include "Sequencer/Phrase.h"
#include "Sequencer/PhraseAnalysis.h"
#include "UI/PhraseHost.h"

#include <array>
#include <atomic>
#include <memory>

// The one adapter between the Phrase layers of a processor and the things around them (docs/mu-tronic/design.md 2.2): it
// gives PhraseContent its PhraseHost and does the work that belongs to no single layer, which is loading a loop (decode and
// detect on a worker, publish on the message thread), laying the slices out, redetecting and the auditions. mu-Tilate's
// processor is one (all its layers are Phrases); mu-Tronic builds one beside its other layer types' services and a slot
// answers only when it holds a Phrase. Message thread unless stated.
namespace mu_tilate
{

class PhraseSlotHost : public PhraseHost
{
public:
    explicit PhraseSlotHost(mu_core::LayerHostView& hostView);
    ~PhraseSlotHost() override;

    // ── PhraseHost ───────────────────────────────────────────────────────────
    juce::AudioProcessorValueTreeState& getPhraseParameters() noexcept override { return view.hostParameters(); }
    // A slot that does not hold a Phrase answers with an unbound stand-in (never rendered), so the content never reads
    // another layer type as a Phrase.
    Phrase& getPhrase(int layer) noexcept override;
    const Phrase& getPhrase(int layer) const noexcept { return const_cast<PhraseSlotHost*>(this)->getPhrase(layer); }
    float getModLive(int layer, int dest) const noexcept override;
    int   getPhraseColourIndex(int layer) const override { return view.hostColourIndex(layer); }
    bool   isPhraseTransportPlaying() const noexcept override { return view.hostTransportPlaying(); }
    double getPhraseTransportBeat() const noexcept override   { return view.hostTransportBeat(); }

    void loadLoopAsync(int layer, const juce::File& file, bool keepSavedEdits = false) override;
    // The same from an already decoded source (no disk), using the section [regionStart, regionEnd) of it.
    void loadLoopAsync(int layer, const juce::File& file, bool keepSavedEdits,
                       std::shared_ptr<const juce::AudioBuffer<float>> decoded, double decodedRate, int regionStart, int regionEnd);
    void redetectSlices(int layer) override;
    void layOutSlices(int layer) override;
    void setSourceRegion(int layer, int start, int end) override;
    void previewSource(int layer, int kind, int slice) override;
    void stopPreview(int layer) override;
    void auditionFile(int layer, const juce::File& file) override;
    juce::File getPrimarySampleDir() const override;
    juce::File getSamplesDir() const override;
    void setPreviewLooping(int layer, bool loop) override;
    void setPlayArea(int layer, int start, int end) override;
    void cropLoop(int layer, int start, int end) override;
    double getPreviewPlayhead(int layer) const override;
    juce::String getLoopStatus(int layer) const override;

    // Loads the loop a layer's preset or session names when the layer does not hold that file's audio. A slot that is not a
    // Phrase, or has no loop path, does nothing.
    void reloadLoopIfNeeded(int layer);
    // reloadLoopIfNeeded for every slot, each Phrase tried once per (layer object, path) so a missing file is not retried
    // every call. A host with no restore step of its own calls this on a timer (mu-Tronic).
    void reloadMissingLoops();

protected:
    // The processor around the layers.
    mu_core::LayerHostView& view;

private:
    // The slot's Phrase, or null when the slot holds another type.
    Phrase* phraseAt(int layer) const noexcept;
    bool    validSlot(int layer) const noexcept { return layer >= 0 && layer < mu_limits::kMaxLayers; }
    // The detection settings from layer `layer`'s parameters (any thread: reads the raw atomics).
    DetectSettings detectSettingsOf(int layer) const;
    double loopTempoOf(int layer) const;
    // Publishes `slices` with a timeline of the layer's length: empty, or (inOrder) the slices back to back from beat 0.
    double layOutInOrder(int layer, const SliceList& slices, double sampleRate, double loopTempo, bool inOrder);

    // Loop loading: a generation per layer (a newer request supersedes an older one), the status line, and a flag the
    // worker's completion checks so it never touches a host that is gone. Detect has its own count, so it neither cancels a
    // loop being loaded nor is cancelled by a load's start.
    std::array<std::atomic<int>, (size_t) mu_limits::kMaxLayers> loopGeneration {};
    std::array<std::atomic<int>, (size_t) mu_limits::kMaxLayers> detectGeneration {};
    std::array<juce::String, (size_t) mu_limits::kMaxLayers>     loopStatus;
    // The Phrase object and path reloadMissingLoops last tried for each slot.
    std::array<std::pair<const Phrase*, juce::String>, (size_t) mu_limits::kMaxLayers> tried;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
    std::unique_ptr<Phrase> standIn;   // built on first need

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhraseSlotHost)
};

} // namespace mu_tilate
