#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Control/ControlSink.h"
#include "Control/LiveView.h"
#include "Control/SceneState.h"
#include "UI/Components/LampState.h"
#include "UI/Components/MuLookAndFeel.h"
#include <array>
#include <functional>

// The scene control (docs/design-ui-family.md 13.9): an engraved "Scene" caption over eight numbered lamp pads, two
// columns by four rows, at the foot of the LayerSidebar. A scene is clip n launched on every layer that stores one, so
// the pad is lit by what ALL the layers say about clip n, and a press sends the same ControlAction the Launchpad does
// (LaunchScene). The pad is the clip strip's own (mu_ui::drawNumberedPad), in the shell accent: a scene belongs to no
// layer. It holds no switching logic, has no store, clear, menu or modifier, and is read-only while Performance mode is
// on (it keeps animating, following the surface).
class SceneGrid : public juce::Component,
                  public juce::TooltipClient,
                  private mu_core::LiveView::Listener,
                  private juce::Timer
{
public:
    using Lamp = mu_ui::LampState;
    static constexpr int kPads    = 8;   // mu_limits::kClipsPerLayer: one scene per clip index
    static constexpr int kColumns = 2;
    static constexpr int kRows    = kPads / kColumns;

    SceneGrid();
    ~SceneGrid() override;

    // What the grid reads and drives; either may be null (then it shows empty pads and does nothing). Not owned.
    void bind(const mu_core::LiveView* view, mu_core::ControlSink* sink);

    // What the status bar shows (name + value); an empty pair clears the line.
    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;

    Lamp getLamp(int pad) const noexcept { return pad >= 0 && pad < kPads ? lamps[(size_t) pad] : Lamp::Empty; }
    int  getLayerCount(int pad) const noexcept { return pad >= 0 && pad < kPads ? stored[(size_t) pad] : 0; }   // layers that store clip `pad`
    bool isReadOnly() const;   // Performance mode is on: a press does nothing

    // ── Pure helpers (tested without a display) ─────────────────────────────
    // The lamp for a scene (mu_core::SceneState, shared with the surface's scene picker): any layer waiting on it = queued;
    // none stores it = empty; every layer that stores it plays it = playing; otherwise stored. No muted state.
    static Lamp lampFor(mu_core::SceneState state) noexcept;
    // The status line's value for a scene: "Scene 3: clip 3 on 9 layers" or "Scene 3: no layer has clip 3".
    static juce::String hoverText(int pad, int layerCount);
    // The click's value: "Clip 3 on 9 layers", or "Clip 3 is empty".
    static juce::String clickText(int pad, int layerCount);

    struct Layout { juce::Rectangle<int> caption; std::array<juce::Rectangle<int>, kPads> pads; };
    static Layout computeLayout(juce::Rectangle<int> bounds);   // caption on top, the pad grid centred under it
    static int    padAt(const Layout& layout, juce::Point<int> p);
    static int    gridWidth();     // two pads and the gap, at the current UI scale
    static int    gridHeight();    // four pads and three gaps
    static int    blockHeight();   // caption + grid

    // A plain press (public so it can be driven without a display): sends LaunchScene(pad); nothing in Performance mode.
    // The status line (a launch's "Clip 3 on 9 layers", or "Queued" while it waits) comes from the live state, so a scene
    // launched from the surface says the same thing.
    void clickPad(int pad);

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
    void say(const juce::String& name, const juce::String& value, bool timed);

    const mu_core::LiveView* view = nullptr;
    mu_core::ControlSink*    sink = nullptr;

    std::array<Lamp, kPads>   lamps {};
    std::array<int, kPads>    stored {};      // how many layers store clip n (of the layer's own type)
    std::array<double, kPads> flashUntil {};
    bool haveBaseline = false;                // false until the first refresh after a (re)bind: that one flashes nothing
    int  queuedScene  = -1;                   // the scene whose "Queued" line is showing: it stays until the scene lands
    int  statusSerial = 0;
    uint32_t seenSceneSerial = 0;             // the last scene launch the status line has reported

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SceneGrid)
};
