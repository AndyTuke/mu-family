#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Control/ControlSink.h"
#include "Control/LiveView.h"
#include "UI/Components/LampState.h"
#include "UI/Components/MuLookAndFeel.h"
#include <array>
#include <functional>

// The layer's clip strip (docs/design-ui-family.md section 13): an engraved "Clip" caption and eight numbered
// lamp pads in the layer's colour, living in the shared LayerHeaderBar. It is the on-screen twin of that layer's
// row of Launchpad pads: it shows what LiveView says, and every press goes to the ControlSink as the same
// ControlActions the surface sends (LaunchClip, which also mutes and cancels; StoreClip / ClearClip, which only
// the screen sends). It holds no switching logic of its own. While Performance mode is on it is read-only
// (the shield swallows the pointer; the strip also refuses presses itself) but keeps animating.
class ClipStrip : public juce::Component,
                  public juce::TooltipClient,
                  private mu_core::LiveView::Listener,
                  private juce::Timer
{
public:
    using Lamp = mu_ui::LampState;
    static constexpr int kPads = 8;   // mu_limits::kClipsPerLayer

    ClipStrip();
    ~ClipStrip() override;

    // What the strip reads and drives; either may be null (then it shows empty pads and does nothing).
    // Neither is owned: the processor outlives the editor.
    void bind(const mu_core::LiveView* view, mu_core::ControlSink* sink);
    void setLayer(int layerIndex);                       // the layer whose clips the pads show
    void setLayerName(const juce::String& name);         // for hover text and dialogs
    void setColour(juce::Colour layerColour);            // the pads' hue: the layer's palette colour
    void setNoun(const juce::String& layerNoun);         // the product's word for a layer, for tooltips

    // What the status bar shows (name + value); an empty pair clears the line.
    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;
    // Asks the user before a destructive step; the default is the shared themed dialog over this component.
    std::function<void(const juce::String& title, const juce::String& message, const juce::String& button,
                       std::function<void()> onConfirm)> askConfirm;

    Lamp getLamp(int pad) const noexcept { return pad >= 0 && pad < kPads ? lamps[(size_t) pad] : Lamp::Empty; }
    bool isReadOnly() const;   // Performance mode is on: nothing here may write or switch

    // ── Pure helpers (tested without a display) ─────────────────────────────
    // The lamp a pad shows from the layer's live clip state; the playing pad flashes when the layer is muted.
    static Lamp lampFor(int pad, int playing, int queued, uint8_t storedMask, bool layerMuted) noexcept;
    struct Layout { juce::Rectangle<int> caption; std::array<juce::Rectangle<int>, kPads> pads; };
    static Layout computeLayout(juce::Rectangle<int> bounds);
    static int    padAt(const Layout& layout, juce::Point<int> p);
    static int    stripWidth();      // caption + pads + gaps, at the current UI scale
    static int    widthWithGap();    // stripWidth plus the gap to the preset dropdown

    // ── Gestures (what a mouse press calls; public so they can be driven without a display) ──
    // A plain click: launch a stored clip (the sink mutes the playing one and cancels a queue), store into an empty pad.
    // Shift: store here, asking first when it would replace a stored clip that is not the playing one.
    void clickPad(int pad, bool shift);
    void storePad(int pad);
    void clearPad(int pad);                    // asks first
    enum MenuId { menuStore = 1, menuClear = 2 };
    void menuChosen(int pad, int menuResult);  // the right-click menu's result (0 = dismissed)

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent& e) override { mouseMove(e); }
    juce::String getTooltip() override;

private:
    void liveStateChanged() override { refresh(); }
    void timerCallback() override { repaint(); updateTimer(); }
    void refresh();
    void updateTimer();
    bool storedAt(int pad) const;
    bool playingAt(int pad) const;
    juce::String clipText(int pad) const;                      // "clip 3"
    std::pair<juce::String, juce::String> hoverFor(int pad) const;
    void say(const juce::String& name, const juce::String& value, bool timed);
    void clearSay();
    void showPopup(int pad);
    void confirm(const juce::String& title, const juce::String& message, const juce::String& button, std::function<void()> go);
    bool send(mu_core::ControlActionType type, int pad);

    const mu_core::LiveView* view = nullptr;
    mu_core::ControlSink*    sink = nullptr;
    int          layer = 0;
    juce::String layerName, noun { "layer" };
    juce::Colour colour { juce::Colours::grey };

    std::array<Lamp, kPads>   lamps {};
    std::array<double, kPads> flashUntil {};
    bool         haveBaseline = false;   // false until the first refresh after a (re)bind: that one flashes nothing
    juce::String queuedStatus;           // the Queued line currently on the status bar ("" = none)
    int          statusSerial = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipStrip)
};
