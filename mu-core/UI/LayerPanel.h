#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Plugin/ProcessorBase.h"
#include "UI/ChannelHeaderBar.h"
#include "UI/ModulatorPanel.h"
#include "UI/Components/MuLookAndFeel.h"

// Shared per-layer panel for every mu-product: the main area to the right of the LayerSidebar.
// Three bands, top to bottom (design-ui-family §5 "Per-layer panel stack"):
//   header strip   kLayerHeaderStripH  the shared ChannelHeaderBar on its own metal strip
//   content        kLayerContentH      the product's sound / sequencer panels (setContent)
//   modulators     the rest           the shared ModulatorPanel, bound to the shown layer
// The shell owns the preset wiring (reset / load / save through the processor's layer-preset
// API), the layer bind (name, colour, modulator slot), the 30 Hz modulator playhead and the
// staging badge. A product supplies only its content component, its modulation destinations,
// and (where it has them) delete / rename behaviour on the header. Metal style only.
class LayerPanel : public juce::Component, private juce::Timer
{
public:
    // `noun` names a layer in dialogs ("Rhythm", "Voice", "Layer", "Track").
    LayerPanel(ProcessorBase& processor, const juce::String& noun);
    ~LayerPanel() override;

    // The product's content component, laid out in the content band. May be called again to
    // swap the content (a combined instance can answer per layer type). Not owned.
    void setContent(juce::Component* content);

    // The product's word for a layer ("Rhythm", "Voice", ...), for dialog titles.
    const juce::String& getNoun() const noexcept { return noun; }

    void setDestProvider(const ModDestProvider* provider) { modulatorPanel.setDestProvider(provider); }

    // Where the modulator playhead reads the song beat from (default: the internal beat).
    void setBeatSource(std::function<double()> source) { beatSource = std::move(source); }

    // Suggested name in the Save dialog (default: the layer's own name).
    void setDefaultPresetName(std::function<juce::String(int)> fn) { presetNameFn = std::move(fn); }

    // A product that keeps its own preset list in the header (mu-Clid: categorised, with its own
    // save dialog) turns the shell's list off and fills the dropdown itself.
    void setOwnsPresetList(bool owns) { ownsPresetList = owns; }

    // A product overlay (a dialog) hosted over the whole panel, above every band.
    void addOverlay(juce::Component& overlay);

    // Products whose layer set is fixed show neither delete nor rename on the header.
    void setFixedLayers(bool fixed);

    // Show layer `idx`: commit any rename in progress, refresh the header and the modulator
    // binding, then tell the product (onLayerChanged) so it rebinds its content.
    void setLayer(int idx);
    int  getLayer() const noexcept { return currentLayer; }

    // Rescan the layer-preset folder into the header's preset list (after a save).
    void refreshPresetList();

    // Re-read the name, colour and preset selection (after a preset load that changed them).
    void refreshHeader();

    ChannelHeaderBar& getHeader() noexcept          { return header; }
    ModulatorPanel&   getModulatorPanel() noexcept  { return modulatorPanel; }

    std::function<void(int)> onLayerChanged;   // the shown layer changed: rebind the content

    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override { resized(); repaint(); }   // screws change the strip's inset

private:
    void timerCallback() override;
    juce::Colour layerColour() const;

    ProcessorBase& proc;
    juce::String   noun;
    int currentLayer = 0;

    ChannelHeaderBar header;
    ModulatorPanel   modulatorPanel;
    juce::Component* content = nullptr;
    std::function<double()> beatSource;
    std::function<juce::String(int)> presetNameFn;
    bool ownsPresetList = true;
    juce::Array<juce::Component*> overlays;

    juce::Rectangle<int> headerR, modR;   // the two panels the shell paints (the content paints its own)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LayerPanel)
};
