#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MuLookAndFeel.h"

// Single-line parameter feedback bar. Shows last-touched param name + value.
// Rhythm colour tag shown when the param belongs to a rhythm.
class StatusBar : public juce::Component,
                  public juce::TooltipClient
{
public:
    StatusBar();

    // Call when any knob changes. rhythmColour is transparent if not a rhythm param.
    void showParam(const juce::String& paramName,
                   const juce::String& value,
                   juce::Colour rhythmColour = juce::Colours::transparentBlack);

    // A small "Support" text link at the right edge (unlicensed copies only). Clicking it calls
    // onSupportClicked; hidden by default.
    void setSupportLinkVisible(bool visible);
    std::function<void()> onSupportClicked;

    // An "Update available" text link, left of the Support link (or at the right edge where there is
    // none). Left click calls onUpdateClicked; right click calls onUpdateHideRequested (the shell
    // offers "Hide this notice"). `tooltip` is the one-line hint shown on hover.
    void setUpdateLinkVisible(bool visible, const juce::String& tooltip = {});
    bool isUpdateLinkVisible() const noexcept { return updateLinkVisible; }
    std::function<void()> onUpdateClicked;
    std::function<void()> onUpdateHideRequested;

    juce::String getTooltip() override { return updateLinkHover ? updateTooltip : juce::String(); }

    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    juce::Rectangle<int> supportLinkBounds() const;
    juce::Rectangle<int> updateLinkBounds() const;
    static juce::Rectangle<int> linkBoundsFor(const juce::String& text, int rightEdge, int height);

    bool supportLinkVisible = false;
    bool supportLinkHover   = false;
    bool updateLinkVisible  = false;
    bool updateLinkHover    = false;
    juce::String updateTooltip;

    juce::String currentName;
    juce::String currentValue;
    juce::Colour tagColour { juce::Colours::transparentBlack };

    static constexpr int kTagWidth = 4;
};
