#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/MuClidLookAndFeel.h"

// Modal overlay showing version, company, and third-party credits.
// Click outside the card → onDismiss fires.
class AboutPanel : public juce::Component
{
public:
    std::function<void()> onDismiss;

    AboutPanel();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    juce::TextButton closeBtn { "Close" };

    static constexpr int kCardW = 400;
    static constexpr int kCardH = 300;
};
