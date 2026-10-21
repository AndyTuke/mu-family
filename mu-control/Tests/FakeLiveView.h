#pragma once

#include "Control/LiveView.h"
#include <array>

// A LiveView with settable fields, for driving the surface model without a processor. `notify()` plays the
// part of LiveState telling its listeners something changed.
namespace mu_control::test
{

struct FakeLiveView : mu_core::LiveView
{
    bool                  mode          = true;
    mu_pp::PerformanceFile performance;
    int                   current       = -1;
    int                   queued        = -1;
    int                   selected      = 0;
    int                   layers        = 4;
    std::array<int, 8>    colourIndex   { 0, 1, 2, 3, 4, 5, 6, 7 };
    std::array<bool, 8>   playable      { true, true, true, true, true, true, true, true };
    uint32_t              revision      = 0;
    std::array<int, 8>     playingClip   { -1, -1, -1, -1, -1, -1, -1, -1 };
    std::array<int, 8>     queuedClip    { -1, -1, -1, -1, -1, -1, -1, -1 };
    std::array<uint8_t, 8> clipMask      {};
    std::array<bool, 8>    muted         {};
    bool                   playing       = false;   // the internal clock runs
    bool                   outside       = false;   // a host / clock owns play
    bool                   otherApp      = false;   // another mu app could take the surface
    bool                   sceneControls = false;   // the product offers the scene picker
    SceneLaunch            lastScene;
    std::array<uint8_t, 8> foreignClips {};         // bit n of layer l set = its clip n is of another layer type

    // Item n is in use (a preset of this product).
    void useItem(int n)
    {
        const juce::File folder("C:/presets");
        performance.setItem(n, "TestState", folder.getChildFile("p" + juce::String(n) + ".preset"), folder, "P" + juce::String(n));
    }

    void notify() { ++revision; listeners.call([](Listener& l) { l.liveStateChanged(); }); }
    int  listenerCount() const { return listeners.size(); }

    bool isPerformanceMode() const noexcept override { return mode; }
    const mu_pp::PerformanceFile& getPerformance() const noexcept override { return performance; }
    juce::File getPerformanceFile() const override { return juce::File(); }
    int  getCurrentItem() const noexcept override { return current; }
    int  getQueuedItem() const noexcept override { return queued; }
    int  getSelectedLayer() const noexcept override { return selected; }
    int  getLayerCount() const override { return layers; }
    int  getLayerColourIndex(int layer) const override { return colourIndex[(size_t) layer]; }
    bool isItemPlayable(int item) const override { return item >= 0 && item < 8 && playable[(size_t) item]; }
    uint32_t getRevision() const noexcept override { return revision; }
    int  getPlayingClip(int layer) const override { return layer >= 0 && layer < 8 ? playingClip[(size_t) layer] : -1; }
    int  getQueuedClip(int layer) const override { return layer >= 0 && layer < 8 ? queuedClip[(size_t) layer] : -1; }
    uint8_t getClipMask(int layer) const override { return layer >= 0 && layer < 8 ? clipMask[(size_t) layer] : (uint8_t) 0; }
    bool isLayerMuted(int layer) const override { return layer >= 0 && layer < 8 && muted[(size_t) layer]; }
    bool isTransportPlaying() const override { return playing; }
    bool isTransportOwnedOutside() const override { return outside; }
    bool isOtherAppAvailable() const override { return otherApp; }
    bool hasSceneControls() const override { return sceneControls; }
    SceneLaunch getLastSceneLaunch() const override { return lastScene; }
    bool isClipForLayer(int layer, int clip) const override
    { return layer < 0 || layer >= 8 || clip < 0 || clip >= 8 || ((foreignClips[(size_t) layer] >> clip) & 1) == 0; }
    void addListener(Listener* l) const override    { listeners.add(l); }
    void removeListener(Listener* l) const override { listeners.remove(l); }

    mutable juce::ListenerList<Listener> listeners;
};

} // namespace mu_control::test
