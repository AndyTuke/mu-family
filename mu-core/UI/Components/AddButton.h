#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MuClidLookAndFeel.h"

// Dashed-border "+ label" button. Click opens caller-supplied PopupMenu.
class AddButton : public juce::Component
{
public:
    std::function<void()> onClick;

    explicit AddButton(const juce::String& label);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

private:
    juce::String labelText;
    bool hovered = false;
};
