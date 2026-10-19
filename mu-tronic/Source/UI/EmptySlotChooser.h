#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Sequencer/LayerTypeRegistry.h"
#include "UI/Components/MuLookAndFeel.h"

#include <functional>
#include <memory>
#include <vector>

namespace mu_tronic
{

// What an Empty slot shows in the main area (docs/mu-tronic/design-ui.md 4.3): one metal panel in the product colour
// with a title ("Add a layer to slot 5") and a card for every layer type the build offers, grouped by product, each
// with an Add button. Choosing a card calls onChoose with the type; the editor builds the layer and shows its panel.
// With nothing registered the panel says so. It only reads the registry: it names no layer type.
class EmptySlotChooser : public juce::Component
{
public:
    explicit EmptySlotChooser(const mu_core::LayerTypeRegistry& registry);

    // The slot the title names (0-based; shown 1-based).
    void setSlot(int slot);
    int  getSlot() const noexcept { return slot; }
    // Re-reads the registry into cards (call after the registry changes).
    void rebuild();
    int  getNumCards() const noexcept { return (int) cards.size(); }

    std::function<void(mu_core::LayerTypeId)> onChoose;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    struct Card
    {
        mu_core::LayerTypeId id = mu_core::LayerTypeId::Empty;
        juce::String name, group;
        juce::Rectangle<int> box;
        std::unique_ptr<juce::TextButton> add;
    };

    const mu_core::LayerTypeRegistry& types;
    int slot = 0;
    std::vector<Card> cards;
    // Per group (in registry order): its heading and its band.
    struct Band { juce::String title; juce::Rectangle<int> area; };
    std::vector<Band> bands;
    juce::Rectangle<int> titleR, hintR;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EmptySlotChooser)
};

} // namespace mu_tronic
