#pragma once

#include "Control/LiveView.h"
#include "MuLimits.h"

// What every layer says about one scene, as one answer: the sidebar's scene pads and a surface's scene picker both
// light a scene from it, so the screen and the pad can never disagree (docs/design-ui-family.md 13.9). A scene is clip n
// launched on every layer that stores one, so the answer is an aggregate over all layers, not the shown page.
// Pure and message-thread: no display, no device.
namespace mu_core
{

// What one layer says about clip n.
struct SceneLayerClip
{
    bool stored  = false;   // the layer stores the clip AND would take it (a clip of another layer type is never launched)
    bool playing = false;   // the clip is the layer's playing clip
    bool queued  = false;   // the clip waits for the layer's boundary
};

// None store it; some store it but not all of those play it; every layer that stores it plays it; some layer waits on it.
// (There is no muted state: a scene has no mute.)
enum class SceneState { Empty, Stored, Playing, Queued };

struct SceneInfo
{
    SceneState state  = SceneState::Empty;
    int        layers = 0;   // how many layers store the clip
};

// A layer waiting on the clip makes the scene Queued whatever the others do; else Empty, Playing or Stored by the rule above.
// `layers` of the result is every layer that stores the clip, in any state.
inline SceneInfo sceneInfoFor(const SceneLayerClip* layers, int count) noexcept
{
    int storing = 0, playing = 0;
    bool anyQueued = false;
    for (int i = 0; i < count; ++i)
    {
        anyQueued = anyQueued || layers[i].queued;
        if (layers[i].stored)
        {
            ++storing;
            if (layers[i].playing) ++playing;
        }
    }
    if (anyQueued) return { SceneState::Queued, storing };
    if (storing == 0) return { SceneState::Empty, 0 };
    return { playing == storing ? SceneState::Playing : SceneState::Stored, storing };
}

// The scene for clip `clip`, read from a LiveView over every layer.
inline SceneInfo sceneInfo(const LiveView& view, int clip) noexcept
{
    if (clip < 0 || clip >= mu_limits::kClipsPerLayer) return {};
    const int layers = view.getLayerCount() < (int) mu_limits::kMaxLayers ? view.getLayerCount() : (int) mu_limits::kMaxLayers;
    SceneLayerClip perLayer[mu_limits::kMaxLayers] {};
    for (int layer = 0; layer < layers; ++layer)
    {
        auto& c   = perLayer[layer];
        c.stored  = ((view.getClipMask(layer) >> clip) & 1) != 0 && view.isClipForLayer(layer, clip);
        c.playing = view.getPlayingClip(layer) == clip;
        c.queued  = view.getQueuedClip(layer) == clip;
    }
    return sceneInfoFor(perLayer, layers);
}

} // namespace mu_core
