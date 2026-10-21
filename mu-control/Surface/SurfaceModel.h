#pragma once

#include "Control/ControlSink.h"
#include "Control/LiveView.h"
#include "Control/SurfaceLook.h"
#include "Surface/SurfaceEvent.h"
#include "Surface/SurfaceMapping.h"
#include "Surface/SurfaceProfile.h"
#include "MuLimits.h"
#include <array>
#include <functional>
#include <vector>

// The device-independent brain of a surface: it turns a pressed control into a ControlAction (through the
// mapping) and decides what every light should show (from the LiveView). It asks the SurfaceDescription what
// the surface can do and degrades to it, and it contains no device name, so one model drives every driver.
// Message thread only. The lights depend only on the LiveView: a press the sink refuses changes nothing.
namespace mu_control
{

class SurfaceModel : private mu_core::LiveView::Listener
{
public:
    // Milliseconds from any steady clock; injected so the blink phase can be tested.
    using Clock = std::function<double()>;

    SurfaceModel(mu_core::ControlSink& sink, const mu_core::LiveView& view, SurfaceMapping mapping,
                 const mu_core::SurfaceLook& look, Clock clock = {}, SurfaceProfile profile = {});
    ~SurfaceModel() override;

    SurfaceModel(const SurfaceModel&) = delete;
    SurfaceModel& operator=(const SurfaceModel&) = delete;

    // A press performs the mapped action (a release or value does nothing in version 1). True when the
    // control is bound, the target exists and the sink accepted the action.
    // With the profile's scene hold down (the logo, in mu-Tronic) a pad press launches that column's scene instead, and the
    // hold's own press and release only open and close the picker (they return false: no action was performed).
    bool handle(const SurfaceEvent& event);

    // True while the scene picker is open (the hold is down in Performance mode).
    bool isScenePickerOpen() const noexcept { return sceneHeld; }
    // The device went away or the mode ended: a release that never arrived must not leave the picker open.
    void closeScenePicker();

    // The whole frame: one LedState per control the surface has (0 = off), final colours with the dim
    // scale and the blink phase applied. The manager diffs it against the last frame.
    std::vector<LedState> render() const;

    // True while some light is animated by this model (a pulse on an RGB surface), so the manager runs its
    // blink timer only then. A surface without RGB blinks natively and never needs the timer.
    bool isAnimating() const;

    // Swap the mapping (a user edit); re-renders through onChanged.
    void setMapping(SurfaceMapping mapping);

    // Called on the message thread when the LiveView changed or the mapping was swapped: render again.
    std::function<void()> onChanged;

private:
    // Flash: square bright / dim (a muted playing clip). Burst: the one brief flash of a clip that just started.
    enum class Level { Off, Dim, Bright, Pulse, Flash, Burst };
    struct Light
    {
        Level    level  = Level::Off;
        uint32_t rgb    = 0;
        bool     settle = false;   // Bright, but a burst ended a moment ago: one more frame must be sent
    };

    void     liveStateChanged() override;
    Light    lightFor(const mu_core::ControlAction& action, double nowMs) const;
    // The mapped action as this product's profile reads it: with layer pages the clip arrows turn the page and the rows are
    // the layers of the shown page.
    mu_core::ControlAction resolve(mu_core::ControlAction action) const;
    void     trackPlayingClips(double nowMs) const;
    uint32_t layerRgb(int layer) const;
    Light    topLight(mu_core::ControlActionType type, double nowMs) const;
    int      arrowSlot(mu_core::ControlActionType type) const noexcept;
    std::vector<LedState> frame(double nowMs, bool* animating) const;
    // Turns a light's level into the final colour and mode the surface can show, and writes it to `id`'s entry of `out`.
    void applyLight(std::vector<LedState>& out, const SurfaceControlId& id, const Light& light, double nowMs, bool* animating) const;
    // The scene picker: every grid column as one scene, the logo lit while the hold is down.
    void lightScenePicker(std::vector<LedState>& out, double nowMs, bool* animating) const;

    mu_core::ControlSink&       sink;
    const mu_core::LiveView&    view;
    SurfaceMapping              mapping;
    std::vector<SurfaceMapping::Binding> bindings;
    mu_core::SurfaceLook        look;
    Clock                       clock;
    SurfaceProfile              profile;
    mutable bool                sceneHeld = false;   // the scene hold is down (reset when the mode ends, hence mutable)
    std::array<double, (size_t) mu_limits::kClipsPerLayer> sceneFlashMs;   // when an accepted press last flashed each scene's column

    // When each layer's playing clip last changed, to time the start-of-clip burst. Written while rendering
    // (message thread only), hence mutable. lastPlaying -2 = not seen yet, so the first frame does not burst.
    mutable std::array<int, (size_t) mu_limits::kMaxLayers>    lastPlaying;
    mutable std::array<double, (size_t) mu_limits::kMaxLayers> burstStartMs;

    // The arrow buttons (AppPrev, AppNext, PerformancePrev, PerformanceNext, PagePrev, PageNext): when an accepted press last
    // flashed each, and which preset arrow was pressed last (it pulses while its switch is queued).
    static constexpr int kArrows = 6;
    std::array<double, kArrows>  arrowFlashMs;
    mutable int                  pulsingArrow = -1;   // arrow slot, -1 = none; cleared once no switch is queued
};

} // namespace mu_control
