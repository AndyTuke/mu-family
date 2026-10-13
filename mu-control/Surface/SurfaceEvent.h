#pragma once

#include <cstdint>
#include <tuple>

// The device-independent language between a driver and the model: which control, what happened to it, and
// what colour to show on it. A driver turns its own numbering and bytes into these and back.
namespace mu_control
{

enum class ControlKind { Pad, Top, Side, Special };

// A control on the surface. x is the column, y the row counted from the TOP of the surface, so the model and
// the mapping never see a device's own numbering. Top buttons: x = index, y = 0. Side buttons: x = 0, y = index
// from the top. A Special button: x = index, y = 0.
struct SurfaceControlId
{
    ControlKind kind = ControlKind::Pad;
    int         x    = 0;
    int         y    = 0;

    friend bool operator==(const SurfaceControlId& a, const SurfaceControlId& b) noexcept
    { return a.kind == b.kind && a.x == b.x && a.y == b.y; }
    friend bool operator!=(const SurfaceControlId& a, const SurfaceControlId& b) noexcept { return ! (a == b); }
    friend bool operator<(const SurfaceControlId& a, const SurfaceControlId& b) noexcept
    { return std::make_tuple((int) a.kind, a.y, a.x) < std::make_tuple((int) b.kind, b.y, b.x); }
};

enum class SurfaceEventType { Press, Release, Value };

struct SurfaceEvent
{
    SurfaceControlId id;
    SurfaceEventType type  = SurfaceEventType::Press;
    float            value = 0.0f;   // Value: the control's reading, 0..1 (pressure, a fader)
};

// The intent of a light. With an RGB surface the model animates and always asks for Static; the others let a
// driver without RGB map Flash / Pulse to its own native blink.
enum class LedMode { Static, Flash, Pulse };

struct LedState
{
    SurfaceControlId id;
    uint32_t         rgb  = 0;   // 0xRRGGBB, final: the dim scale and the blink phase are already applied; 0 = off
    LedMode          mode = LedMode::Static;

    friend bool operator==(const LedState& a, const LedState& b) noexcept
    { return a.id == b.id && a.rgb == b.rgb && a.mode == b.mode; }
    friend bool operator!=(const LedState& a, const LedState& b) noexcept { return ! (a == b); }
};

} // namespace mu_control
