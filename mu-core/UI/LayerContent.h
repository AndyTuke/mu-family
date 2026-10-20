#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

struct ModDestProvider;

// A layer type's content component for the shown slot (the middle band of the shared LayerPanel). A host of
// several types (mu-Tronic) keeps one instance per type and rebinds it as the selection moves between slots of
// that type; it swaps the instance into the panel only when the type changes. Each product's <Type>Content derives
// from this, so the host needs nothing else from it (docs/mu-tronic/design.md 2.1.11, design-ui.md 4.5a).
class LayerContent : public juce::Component
{
public:
    // Show slot `slot`: rebind every control, attachment and modulation arc to that slot's layer.
    virtual void setLayer(int slot) = 0;

    // Let go of every binding that points into the shown layer (the modulation arcs hold its matrix). Called before
    // the layer is removed, replaced or moved; setLayer rebinds.
    virtual void releaseModulationArcs() = 0;

    // The modulation destinations the shared modulator panel offers for this type's layers; null = none.
    virtual const ModDestProvider* getModDestProvider() const noexcept { return nullptr; }

    // The modulators of the layer in `slot` were edited (an assignment, a mode or a destination): a type that caches
    // something derived from them (the Pattern's stepped-pitch flags) refreshes it. Message thread. Default: nothing.
    virtual void modulatorsChanged(int slot) { juce::ignoreUnused(slot); }

    // A knob hover or change: the control's name and value, for the shell's status bar.
    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;
};
