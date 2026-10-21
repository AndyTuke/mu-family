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

    // The CPU readout at the extreme right edge ("CPU 23%", docs/design-ui-family.md section 6). Off by default. `load` is
    // the fraction of the audio callback's time used (1.0 = all of it), the peak of the last second; it shows as a whole
    // percent, the number amber from kCpuWarnAt and red from kCpuHotAt. It repaints at most four times a second so the
    // digits stay readable. When on, the Update and Support links sit left of it.
    void setCpuMeterVisible(bool visible);
    bool isCpuMeterVisible() const noexcept { return cpuVisible; }
    void setCpuLoad(float load);
    int  getCpuPercent() const noexcept { return cpuPercent; }

    // The shown percent for a load fraction (rounded, never below 0) and the band its number is drawn in.
    enum class CpuLevel { Normal, Warn, Hot };
    static int      cpuPercentFor(float load) noexcept;
    static CpuLevel cpuLevelFor(float load) noexcept;
    static constexpr int kCpuRepaintMs = 200;   // the shortest time between two repaints of the readout
    // Where the Update and Support links' right edge is for a bar `width` wide: 4 px in from the edge, and with the readout
    // on, a further slot and gap to its left.
    static int linksRightEdgeFor(int width, bool cpuVisible) noexcept;

    juce::String getTooltip() override;

    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    juce::Rectangle<int> supportLinkBounds() const;
    juce::Rectangle<int> updateLinkBounds() const;
    juce::Rectangle<int> cpuBounds() const;
    static juce::Rectangle<int> linkBoundsFor(const juce::String& text, int rightEdge, int height);

    bool supportLinkVisible = false;
    bool supportLinkHover   = false;
    bool updateLinkVisible  = false;
    bool updateLinkHover    = false;
    bool cpuVisible         = false;
    int  cpuPercent         = 0;
    juce::uint32 cpuPaintedAt = 0;   // millisecond counter at the readout's last repaint
    juce::String updateTooltip;

    juce::String currentName;
    juce::String currentValue;
    juce::Colour tagColour { juce::Colours::transparentBlack };

    static constexpr int kTagWidth = 4;
};
