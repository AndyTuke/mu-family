#include "Surface/SurfaceModel.h"
#include <algorithm>
#include <cmath>

namespace mu_control
{

namespace
{
// Scales a 0xRRGGBB colour's brightness.
uint32_t scaleRgb(uint32_t rgb, float s) noexcept
{
    const auto ch = [&](int shift)
    {
        const float v = (float) ((rgb >> shift) & 0xFFu) * s;
        return (uint32_t) std::lround(std::min(255.0f, std::max(0.0f, v)));
    };
    return (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

// Smooth 0..1 pulse at `hz`: starts at 0 (dim), peaks half a period later.
float pulsePhase(double nowMs, float hz) noexcept
{
    const double turns = nowMs * 0.001 * (double) hz;
    return (float) (0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * turns));
}
}

SurfaceModel::SurfaceModel(mu_core::ControlSink& s, const mu_core::LiveView& v, SurfaceMapping m,
                           const mu_core::SurfaceLook& l, Clock c)
    : sink(s), view(v), mapping(std::move(m)), bindings(mapping.bindings()), look(l), clock(std::move(c))
{
    if (! clock) clock = [] { return juce::Time::getMillisecondCounterHiRes(); };
    view.addListener(this);
}

SurfaceModel::~SurfaceModel() { view.removeListener(this); }

void SurfaceModel::liveStateChanged()
{
    if (onChanged) onChanged();
}

void SurfaceModel::setMapping(SurfaceMapping m)
{
    mapping = std::move(m);
    bindings = mapping.bindings();
    if (onChanged) onChanged();
}

bool SurfaceModel::handle(const SurfaceEvent& event)
{
    if (event.type != SurfaceEventType::Press) return false;
    const auto action = mapping.actionFor(event.id);
    if (! action) return false;

    // A layer that does not exist is a dark pad: pressing it does nothing.
    using T = mu_core::ControlActionType;
    if ((action->type == T::SelectLayer || action->type == T::LaunchClip)
        && (action->layer < 0 || action->layer >= view.getLayerCount()))
        return false;

    return sink.perform(*action);
}

uint32_t SurfaceModel::layerRgb(int layer) const
{
    const int idx = std::max(0, std::min((int) look.layerRgb.size() - 1, view.getLayerColourIndex(layer)));
    return look.layerRgb[(size_t) idx];
}

SurfaceModel::Light SurfaceModel::lightFor(const mu_core::ControlAction& a) const
{
    using T = mu_core::ControlActionType;
    switch (a.type)
    {
        case T::PerformanceItem:
        {
            // Top row: current bright, queued pulsing, available dim, a missing or foreign item dim red, empty off.
            const int n = a.index;
            if (n < 0 || n >= (int) view.getPerformance().items.size() || ! view.getPerformance().items[(size_t) n].used)
                return {};
            if (view.getCurrentItem() == n)  return { Level::Bright, look.accentRgb };
            if (view.getQueuedItem() == n)   return { Level::Pulse, look.accentRgb };
            if (view.isItemPlayable(n))      return { Level::Dim, look.accentRgb };
            return { Level::Dim, look.faultRgb };
        }
        case T::SelectLayer:
        {
            // Side column: the selected layer bright, every other existing layer dim, no layer off.
            if (a.layer < 0 || a.layer >= view.getLayerCount()) return {};
            return { view.getSelectedLayer() == a.layer ? Level::Bright : Level::Dim, layerRgb(a.layer) };
        }
        case T::LaunchClip:
            // The clip getters arrive with the clip bank; until then the grid stays dark.
            return {};
        default:
            return {};
    }
}

std::vector<LedState> SurfaceModel::frame(double nowMs, bool* animating) const
{
    const auto& d = mapping.getDescription();
    std::vector<LedState> out;
    for (const auto& id : controlsOf(d)) out.push_back({ id, 0, LedMode::Static });
    if (animating != nullptr) *animating = false;
    if (! view.isPerformanceMode()) return out;   // outside the mode the surface is dark

    for (const auto& b : bindings)
    {
        const auto light = lightFor(b.action);
        if (light.level == Level::Off) continue;

        // Phase: pick the final colour and mode for what the surface can show.
        uint32_t rgb  = light.rgb;
        LedMode  mode = LedMode::Static;
        switch (light.level)
        {
            case Level::Bright: break;
            case Level::Dim:    rgb = scaleRgb(rgb, look.dimScale); break;
            case Level::Pulse:
                if (d.colour == ColourSupport::Rgb)   // animate it here, sent as plain colours
                {
                    rgb = scaleRgb(rgb, look.dimScale + (1.0f - look.dimScale) * pulsePhase(nowMs, look.pulseHz));
                    if (animating != nullptr) *animating = true;
                }
                else if (d.canPulse)                  // the device blinks natively
                    mode = LedMode::Pulse;
                else                                  // no pulse at all: a steady level between dim and bright
                    rgb = scaleRgb(rgb, 0.5f * (1.0f + look.dimScale));
                break;
            case Level::Off: break;
        }

        const auto it = std::find_if(out.begin(), out.end(), [&](const LedState& l) { return l.id == b.id; });
        if (it != out.end()) { it->rgb = rgb; it->mode = mode; }
    }
    return out;
}

std::vector<LedState> SurfaceModel::render() const { return frame(clock(), nullptr); }

bool SurfaceModel::isAnimating() const
{
    bool animating = false;
    frame(clock(), &animating);
    return animating;
}

} // namespace mu_control
