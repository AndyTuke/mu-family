#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/MuLookAndFeel.h"

// Modal "About" overlay shared across the mu-family. Each product passes its
// display name + third-party credits via setProductInfo() before showing.
// Click outside the card → onDismiss fires.
class AboutOverlay : public juce::Component
{
public:
    std::function<void()> onDismiss;

    AboutOverlay();

    void setProductInfo(const juce::String& displayName,
                        const juce::StringArray& credits);

    // Support entry points. An unlicensed copy shows "Support" (opens the pay-what-you-want
    // prompt) and "Enter a key"; a supporter sees a thank-you line and a "Want to give more?" tip link instead; a product with no
    // licensing shows neither. Set each time the panel opens.
    enum class SupportState { None, Unlicensed, Supporter };
    void setSupportState(SupportState state);
    std::function<void()> onSupport;
    std::function<void()> onEnterKey;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    juce::TextButton closeBtn   { "Close" };
    juce::TextButton supportBtn { "Support" };
    juce::TextButton keyBtn     { "Enter a key" };
    juce::HyperlinkButton tipLink;   // supporters only: opens the shared tip page
    SupportState     supportState = SupportState::None;

    juce::String      productName;
    juce::StringArray productCredits;

    static constexpr int kCardW = 400;
    static constexpr int kCardH = 300;
};
