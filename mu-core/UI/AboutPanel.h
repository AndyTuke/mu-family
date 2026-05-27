#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/MuLookAndFeel.h"

// Modal "About" overlay shared across the mu-family. Each product passes its
// display name + third-party credits via setProductInfo() before showing.
// Click outside the card → onDismiss fires.
class AboutPanel : public juce::Component
{
public:
    std::function<void()> onDismiss;

    AboutPanel();

    void setProductInfo(const juce::String& displayName,
                        const juce::StringArray& credits);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    juce::TextButton closeBtn { "Close" };

    juce::String      productName;
    juce::StringArray productCredits;

    static constexpr int kCardW = 400;
    static constexpr int kCardH = 300;
};
