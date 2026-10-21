#include "Surface/SurfaceModel.h"
#include "Control/SceneState.h"
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

// How long past the burst the blink timer keeps running, so the steady frame after the flash is sent.
constexpr double kBurstTailMs = 100.0;

// Pulls a colour toward white by `amount` (0..1): the start-of-clip burst.
uint32_t toWhite(uint32_t rgb, float amount) noexcept
{
    const auto ch = [&](int shift)
    {
        const float v = (float) ((rgb >> shift) & 0xFFu);
        return (uint32_t) std::lround(v + (255.0f - v) * amount);
    };
    return (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

// The arrows' resting white: below full so the white flash of a press stands out against it. Untuned on a real pad.
constexpr float kArrowRestScale = 0.75f;

// How many performance items can be played now; the preset arrows are dark with fewer than two.
int playableItems(const mu_core::LiveView& view) noexcept
{
    int n = 0;
    const auto& items = view.getPerformance().items;
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].used && view.isItemPlayable((int) i)) ++n;
    return n;
}

int countBits(unsigned v) noexcept
{
    int n = 0;
    for (; v != 0; v &= v - 1) ++n;
    return n;
}

// Smooth 0..1 pulse at `hz`: starts at 0 (dim), peaks half a period later.
float pulsePhase(double nowMs, float hz) noexcept
{
    const double turns = nowMs * 0.001 * (double) hz;
    return (float) (0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * turns));
}
}

SurfaceModel::SurfaceModel(mu_core::ControlSink& s, const mu_core::LiveView& v, SurfaceMapping m,
                           const mu_core::SurfaceLook& l, Clock c, SurfaceProfile p)
    : sink(s), view(v), mapping(std::move(m)), bindings(mapping.bindings()), look(l), clock(std::move(c)), profile(std::move(p))
{
    sceneFlashMs.fill(-1.0e12);
    lastPlaying.fill(-2);
    burstStartMs.fill(-1.0e12);
    arrowFlashMs.fill(-1.0e12);
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

void SurfaceModel::closeScenePicker()
{
    if (! sceneHeld) return;
    sceneHeld = false;
    if (onChanged) onChanged();
}

bool SurfaceModel::handle(const SurfaceEvent& event)
{
    // The scene hold: its press opens the picker and its release closes it. Not a ControlAction, so it is read before the mapping.
    if (profile.sceneHold && event.id == *profile.sceneHold)
    {
        const bool down = event.type == SurfaceEventType::Press && view.isPerformanceMode();
        if (event.type != SurfaceEventType::Value && down != sceneHeld)
        {
            sceneHeld = down;
            if (onChanged) onChanged();
        }
        return false;
    }

    if (event.type != SurfaceEventType::Press) return false;

    // While the hold is down a pad is a scene: column n launches clip n on every layer that stores one. A column nobody
    // stores is dark and shows no light, but the press still goes to the sink so the screen can say the scene is empty.
    if (sceneHeld && event.id.kind == ControlKind::Pad)
    {
        if (event.id.x < 0 || event.id.x >= mu_limits::kClipsPerLayer) return false;
        mu_core::ControlAction scene;
        scene.type  = mu_core::ControlActionType::LaunchScene;
        scene.index = event.id.x;
        const bool lit      = mu_core::sceneInfo(view, event.id.x).state != mu_core::SceneState::Empty;
        const bool accepted = sink.perform(scene);
        if (accepted && lit)
        {
            sceneFlashMs[(size_t) event.id.x] = clock();   // the column flashes white once
            if (onChanged) onChanged();
        }
        return accepted;
    }

    const auto mapped = mapping.actionFor(event.id);
    if (! mapped) return false;
    const auto action = std::optional<mu_core::ControlAction>(resolve(*mapped));

    // A layer that does not exist is a dark pad: pressing it does nothing.
    using T = mu_core::ControlActionType;
    if (action->type == T::StoreClip || action->type == T::ClearClip) return false;   // a pad never writes a clip
    if ((action->type == T::SelectLayer || action->type == T::LaunchClip)
        && (action->layer < 0 || action->layer >= view.getLayerCount()))
        return false;

    // A host, MIDI clock or Link owns play: the transport button is inert and shows it.
    if (action->type == T::TransportToggle && view.isTransportOwnedOutside()) return false;

    const bool accepted = sink.perform(*action);

    // An accepted arrow press flashes its button; a preset arrow also pulses until the switch it queued commits.
    const int arrow = arrowSlot(action->type);
    if (accepted && arrow >= 0)
    {
        arrowFlashMs[(size_t) arrow] = clock();
        if (arrow == 2 || arrow == 3) pulsingArrow = arrow;   // only the preset arrows wait on a queued switch
        if (onChanged) onChanged();   // the lights now differ from the frame drawn while the sink performed
    }
    return accepted;
}

int SurfaceModel::arrowSlot(mu_core::ControlActionType type) const noexcept
{
    using T = mu_core::ControlActionType;
    switch (type)
    {
        case T::AppPrev:         return 0;
        case T::AppNext:         return 1;
        case T::PerformancePrev: return 2;
        case T::PerformanceNext: return 3;
        case T::PagePrev:        return 4;
        case T::PageNext:        return 5;
        default:                 return -1;
    }
}

SurfaceModel::Light SurfaceModel::topLight(mu_core::ControlActionType type, double nowMs) const
{
    using T = mu_core::ControlActionType;

    // Phase 1: the steady light this button has when nothing was just pressed.
    Light steady;
    switch (type)
    {
        case T::AppPrev:
        case T::AppNext:   // the app accent, only when another app could take the surface
            if (view.isOtherAppAvailable()) steady = { Level::Bright, look.accentRgb };
            break;
        case T::PerformancePrev:
        case T::PerformanceNext:   // white, dark when there is nothing to step to
            if (playableItems(view) >= 2) steady = { Level::Bright, scaleRgb(look.whiteRgb, kArrowRestScale) };
            break;
        case T::PagePrev:
        case T::PageNext:   // white while there is a page that way; dim at the end (no wrap), the on-pad page indicator
        {
            const bool more = type == T::PagePrev ? view.getShownPage() > 0 : view.getShownPage() < view.getPageCount() - 1;
            steady = more ? Light { Level::Bright, scaleRgb(look.whiteRgb, kArrowRestScale) } : Light { Level::Dim, look.whiteRgb };
            break;
        }
        case T::ClipPrev:
        case T::ClipNext:   // dim white when the selected layer has two stored clips to step between
        {
            const int sel = view.getSelectedLayer();
            if (sel >= 0 && sel < view.getLayerCount() && countBits(view.getClipMask(sel)) >= 2)
                steady = { Level::Dim, look.whiteRgb };
            break;
        }
        case T::MuteSelectedLayer:   // the selected layer's colour: dim = playing, flashing = muted
        {
            const int sel = view.getSelectedLayer();
            if (sel >= 0 && sel < view.getLayerCount())
                steady = { view.isLayerMuted(sel) ? Level::Flash : Level::Dim, layerRgb(sel) };
            break;
        }
        case T::TransportToggle:   // the light shows what a press does; static dim white when the transport is owned elsewhere
            if (view.isTransportOwnedOutside())    steady = { Level::Dim, look.whiteRgb };
            else if (view.isTransportPlaying())    steady = { Level::Bright, look.faultRgb };
            else                                   steady = { Level::Bright, look.goRgb };
            break;
        default: break;
    }

    // Phase 2: an arrow shows a just-accepted press (white flash) or its queued switch (white pulse).
    const int arrow = arrowSlot(type);
    if (steady.level == Level::Off || arrow < 0) return steady;
    const double since = nowMs - arrowFlashMs[(size_t) arrow];
    if (since >= 0.0 && since < (double) look.stepFlashMs) return { Level::Burst, look.whiteRgb, false };
    if ((arrow == 2 || arrow == 3) && pulsingArrow == arrow && view.getQueuedItem() >= 0) return { Level::Pulse, look.whiteRgb, false };
    steady.settle = since >= 0.0 && since < (double) look.stepFlashMs + kBurstTailMs;
    return steady;
}

mu_core::ControlAction SurfaceModel::resolve(mu_core::ControlAction a) const
{
    using T = mu_core::ControlActionType;
    if (! profile.layerPages) return a;
    if (a.type == T::ClipPrev) a.type = T::PagePrev;
    else if (a.type == T::ClipNext) a.type = T::PageNext;
    else if (a.type == T::SelectLayer || a.type == T::LaunchClip) a.layer += view.getShownPage() * mu_limits::kLayersPerPage;
    return a;
}

uint32_t SurfaceModel::layerRgb(int layer) const
{
    const int idx = std::max(0, std::min((int) look.layerRgb.size() - 1, view.getLayerColourIndex(layer)));
    return look.layerRgb[(size_t) idx];
}

void SurfaceModel::trackPlayingClips(double nowMs) const
{
    // A layer whose playing clip changed to a real clip starts a burst; the first sighting does not.
    for (int layer = 0; layer < mu_limits::kMaxLayers; ++layer)
    {
        const int playing = view.getPlayingClip(layer);
        auto& last = lastPlaying[(size_t) layer];
        if (playing == last) continue;
        if (last != -2 && playing >= 0) burstStartMs[(size_t) layer] = nowMs;
        last = playing;
    }
}

SurfaceModel::Light SurfaceModel::lightFor(const mu_core::ControlAction& a, double nowMs) const
{
    using T = mu_core::ControlActionType;
    switch (a.type)
    {
        case T::AppPrev:
        case T::AppNext:
        case T::PerformancePrev:
        case T::PerformanceNext:
        case T::PagePrev:
        case T::PageNext:
        case T::ClipPrev:
        case T::ClipNext:
        case T::MuteSelectedLayer:
        case T::TransportToggle:
            return topLight(a.type, nowMs);   // the top row (PerformanceItem has no light: it is for MIDI mapping only)
        case T::SelectLayer:
        {
            // Side column: the selected layer bright, every other existing layer dim, no layer off.
            if (a.layer < 0 || a.layer >= view.getLayerCount()) return {};
            return { view.getSelectedLayer() == a.layer ? Level::Bright : Level::Dim, layerRgb(a.layer) };
        }
        case T::LaunchClip:
        {
            // Grid: a row is a layer, a column a clip. Playing bright (flashing when its layer is muted, a
            // burst when it just started), queued pulsing, stored dim, empty off, no layer off.
            const int layer = a.layer, clip = a.index;
            if (layer < 0 || layer >= view.getLayerCount() || clip < 0 || clip >= mu_limits::kClipsPerLayer) return {};
            const auto rgb = layerRgb(layer);
            if (view.getPlayingClip(layer) == clip)
            {
                if (view.isLayerMuted(layer)) return { Level::Flash, rgb, false };
                const double sinceStart = nowMs - burstStartMs[(size_t) layer];
                if (sinceStart >= 0.0 && sinceStart < (double) look.stepFlashMs) return { Level::Burst, rgb, false };
                return { Level::Bright, rgb, sinceStart >= 0.0 && sinceStart < (double) look.stepFlashMs + kBurstTailMs };
            }
            if (view.getQueuedClip(layer) == clip) return { Level::Pulse, rgb, false };
            if ((view.getClipMask(layer) & (1u << clip)) != 0) return { Level::Dim, rgb, false };
            return {};
        }
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
    trackPlayingClips(nowMs);                     // before the mode test, so entering the mode does not burst every pad
    if (view.getQueuedItem() < 0) pulsingArrow = -1;   // the switch landed or was cancelled: the arrow stops pulsing
    if (! view.isPerformanceMode()) { sceneHeld = false; return out; }   // outside the mode the surface is dark, and no hold survives it

    for (const auto& b : bindings)
    {
        const auto light = lightFor(resolve(b.action), nowMs);
        if (light.level == Level::Off) continue;
        applyLight(out, b.id, light, nowMs, animating);
    }

    if (sceneHeld) lightScenePicker(out, nowMs, animating);   // over the grid: the picker replaces what the pads meant
    return out;
}

void SurfaceModel::applyLight(std::vector<LedState>& out, const SurfaceControlId& id, const Light& light, double nowMs, bool* animating) const
{
    const auto& d = mapping.getDescription();
    {
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
            case Level::Flash:
            {
                const double turns  = nowMs * 0.001 * (double) look.flashHz;
                const bool   bright = turns - std::floor(turns) < 0.5;   // square, 50 % duty; the dim half is never dark
                if (d.colour == ColourSupport::Rgb)
                {
                    if (! bright) rgb = scaleRgb(rgb, look.dimScale);
                    if (animating != nullptr) *animating = true;
                }
                else if (d.canFlash)                  // the device flashes natively
                    mode = LedMode::Flash;
                else                                  // no flash at all: a steady level between dim and bright
                    rgb = scaleRgb(rgb, 0.5f * (1.0f + look.dimScale));
                break;
            }
            case Level::Burst:
                if (d.colour == ColourSupport::Rgb)   // only an RGB pad can show a brighter-than-bright flash
                {
                    rgb = toWhite(rgb, 0.6f);
                    if (animating != nullptr) *animating = true;
                }
                break;
            case Level::Off: break;
        }
        if (light.settle && d.colour == ColourSupport::Rgb && animating != nullptr) *animating = true;

        const auto it = std::find_if(out.begin(), out.end(), [&](const LedState& l) { return l.id == id; });
        if (it != out.end()) { it->rgb = rgb; it->mode = mode; }
    }
}

// The scene picker (mu-Tronic): each grid column c is scene c, lit as a whole in white, the colour no layer uses. Playing
// (every layer that stores the clip plays it) full white; stored on some layer white x kArrowRestScale; waiting on some
// layer pulses; nobody stores it: dark. An accepted press flashes its column once. The logo is lit white while it is held.
void SurfaceModel::lightScenePicker(std::vector<LedState>& out, double nowMs, bool* animating) const
{
    const auto& d = mapping.getDescription();
    const int columns = std::min(d.gridWidth, (int) mu_limits::kClipsPerLayer);
    for (int x = 0; x < columns; ++x)
    {
        Light light;   // Off: a column nobody stores is dark, and that also clears what the pad meant a moment ago
        switch (mu_core::sceneInfo(view, x).state)
        {
            case mu_core::SceneState::Playing: light = { Level::Bright, look.whiteRgb }; break;
            case mu_core::SceneState::Stored:  light = { Level::Bright, scaleRgb(look.whiteRgb, kArrowRestScale) }; break;
            case mu_core::SceneState::Queued:  light = { Level::Pulse, look.whiteRgb }; break;
            case mu_core::SceneState::Empty:   break;
        }
        const double since = nowMs - sceneFlashMs[(size_t) x];
        if (light.level != Level::Off)
        {
            if (since >= 0.0 && since < (double) look.stepFlashMs) light = { Level::Burst, look.whiteRgb, false };
            else light.settle = light.level == Level::Bright && since >= 0.0 && since < (double) look.stepFlashMs + kBurstTailMs;
        }
        for (int y = 0; y < d.gridHeight; ++y)
        {
            const SurfaceControlId pad { ControlKind::Pad, x, y };
            const auto it = std::find_if(out.begin(), out.end(), [&](const LedState& l) { return l.id == pad; });
            if (it != out.end()) { it->rgb = 0; it->mode = LedMode::Static; }
            if (light.level != Level::Off) applyLight(out, pad, light, nowMs, animating);
        }
    }

    if (profile.sceneHold && hasControl(d, *profile.sceneHold)) applyLight(out, *profile.sceneHold, { Level::Bright, look.whiteRgb }, nowMs, animating);
}

std::vector<LedState> SurfaceModel::render() const { return frame(clock(), nullptr); }

bool SurfaceModel::isAnimating() const
{
    bool animating = false;
    frame(clock(), &animating);
    return animating;
}

} // namespace mu_control
