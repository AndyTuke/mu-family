#pragma once

#include "Control/ControlSink.h"
#include "Control/LiveView.h"
#include "Control/SurfaceLook.h"
#include "Surface/SurfaceEvent.h"
#include "Surface/SurfaceMapping.h"
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
                 const mu_core::SurfaceLook& look, Clock clock = {});
    ~SurfaceModel() override;

    SurfaceModel(const SurfaceModel&) = delete;
    SurfaceModel& operator=(const SurfaceModel&) = delete;

    // A press performs the mapped action (a release or value does nothing in version 1). True when the
    // control is bound, the target exists and the sink accepted the action.
    bool handle(const SurfaceEvent& event);

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
    enum class Level { Off, Dim, Bright, Pulse };
    struct Light { Level level = Level::Off; uint32_t rgb = 0; };

    void     liveStateChanged() override;
    Light    lightFor(const mu_core::ControlAction& action) const;
    uint32_t layerRgb(int layer) const;
    std::vector<LedState> frame(double nowMs, bool* animating) const;

    mu_core::ControlSink&       sink;
    const mu_core::LiveView&    view;
    SurfaceMapping              mapping;
    std::vector<SurfaceMapping::Binding> bindings;
    mu_core::SurfaceLook        look;
    Clock                       clock;
};

} // namespace mu_control
