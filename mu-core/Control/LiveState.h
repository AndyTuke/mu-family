#pragma once

#include "Control/ControlAction.h"
#include "Control/LiveView.h"
#include "MuLimits.h"
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <functional>

// The live state behind Performance mode (docs/design-plugin-family.md, "Performance mode and live
// state"): the loaded performance, which item plays and which is queued, the selected layer. Owned by
// the instance (ProcessorBase), so a pad press needs no window; plain class, no processor dependency,
// MESSAGE THREAD ONLY (no atomics, no locks; the audio thread never touches it).
//
// It also holds the clip bank's live half: per layer which clip plays, which waits for its boundary and
// which are stored (the bank itself lives on the Layer; docs/design-plugin-family.md "Clip bank").
//
// It performs the actions that change that state (PerformanceMode / Item / Prev / Next, SelectLayer,
// LaunchClip / ClipNext / ClipPrev).
// How a preset is actually staged or cancelled is supplied by the owner as callbacks, so this class
// never names a product, a processor or the hot-swap machinery. `perform` never quantises: the swap
// lands at the boundary the preset's swap timing sets, never twice-quantised.
namespace mu_core
{

class LiveState : public LiveView
{
public:
    // What staging an item did: it failed to load, it is waiting for its boundary, or it was applied at once.
    enum class Stage { Failed, Queued, Applied };

    struct Callbacks
    {
        std::function<bool()>      supportsPerformanceMode;   // this product and build allow the mode
        std::function<bool(int)>   canLoad;                   // item n is this product's and its file exists
        std::function<Stage(int)>  stageItem;                 // stage item n's full preset
        std::function<void()>      cancelItem;                // drop the staged full preset
        std::function<int()>       layerCount;                // how many layers are live (bounds SelectLayer)
        std::function<int(int)>    layerColourIndex;          // layer n's index into the shared layer palette
        std::function<bool()>      fullPending;               // some full preset is staged and not yet committed
        std::function<uint32_t()>  stageSerial;               // changes whenever ANY full preset is staged (identity)
        std::function<uint32_t()>  fullCommitCount;           // changes whenever a full preset is actually applied (never on a stage or cancel)

        // The clip bank. All optional: without them every clip action is refused.
        std::function<bool(int, int)> launchClip;            // stage clip c on layer n (false = refused, nothing staged)
        std::function<bool(int)>      slotPending;           // layer n has a staged layer preset not yet committed
        std::function<uint32_t(int)>  slotSerial;            // changes whenever layer n is staged or cancelled (identity)
        std::function<void(int)>      cancelSlot;            // drop layer n's staged layer preset
        std::function<bool(int)>      muteLayer;             // flip layer n's strip mute (the MuteLayer path)
        std::function<bool(int)>      layerMuted;            // is layer n's strip muted
        std::function<void(int, int)> clipPlaying;           // layer n now plays clip c (the owner records it with the layer)
        std::function<int(int)>       savedClip;             // the clip layer n was saved playing (-1 = none, or it is not stored any more)
    };

    void setCallbacks(Callbacks c) { cb = std::move(c); }

    // ── LiveView ─────────────────────────────────────────────────────────────
    bool                          isPerformanceMode() const noexcept override { return performanceMode; }
    const mu_pp::PerformanceFile& getPerformance()    const noexcept override { return performance; }
    juce::File                    getPerformanceFile() const override         { return performanceFile; }
    int                           getCurrentItem()    const noexcept override { return currentItem; }
    int                           getQueuedItem()     const noexcept override { return queuedItem; }
    int                           getSelectedLayer()  const noexcept override { return selectedLayer; }
    int                           getLayerCount()     const override       { return cb.layerCount ? cb.layerCount() : 1; }
    int                           getLayerColourIndex(int layer) const override
    { return cb.layerColourIndex ? cb.layerColourIndex(layer) : (layer < 0 ? 0 : layer % 8); }
    bool                          isItemPlayable(int item) const override    { return itemLoadable(item); }
    uint32_t                      getRevision()       const noexcept override { return revision; }
    int                           getPlayingClip(int layer) const override { return inRange(layer) ? clipState[(size_t) layer].playing : -1; }
    int                           getQueuedClip(int layer)  const override { return inRange(layer) ? clipState[(size_t) layer].queued : -1; }
    uint8_t                       getClipMask(int layer)    const override { return inRange(layer) ? clipState[(size_t) layer].mask : (uint8_t) 0; }
    bool                          isLayerMuted(int layer)   const override
    { return layer >= 0 && layer < getLayerCount() && cb.layerMuted && cb.layerMuted(layer); }
    void addListener(Listener* l) const override    { listeners.add(l); }
    void removeListener(Listener* l) const override { listeners.remove(l); }

    // ── Loading a performance (a screen-only call, made before the mode is entered) ──
    // Replaces the loaded list and forgets which item played. Refused while the mode is on.
    bool loadPerformance(mu_pp::PerformanceFile file, const juce::File& where);

    // ── Actions ──────────────────────────────────────────────────────────────
    // True for the types this class owns: PerformanceMode, PerformanceItem, PerformancePrev, PerformanceNext, SelectLayer,
    // LaunchClip, ClipNext, ClipPrev.
    static bool handles(ControlActionType t) noexcept;

    // Performs one of those. Every one but PerformanceMode and LaunchClip returns false while the mode is off
    // (the on-screen clip strip launches outside the mode; the surface only exists inside it).
    // PerformanceMode: `index` 1 = on, 0 = off, -1 (the default) = toggle; refused when the product does not
    // support the mode or no performance is loaded; turning it on takes `layer` as the selected layer.
    // LaunchClip(layer, index): the playing clip's pad cancels a queued launch on that layer, else toggles the
    // layer's mute; an empty pad does nothing; any other stored clip launches (playing at once when the
    // transport is stopped, queued until its commit otherwise). ClipNext / ClipPrev step the SELECTED layer
    // to the next / previous stored clip, wrapping, and never mute.
    bool perform(const ControlAction& action);

    // The owner's "a full-preset swap just committed" hook (message thread): the queued item becomes
    // current when the preset that landed is the one this class staged; a preset loaded some other way
    // (browser, MIDI program change) leaves no item current. The clip state restarts from what the new layers
    // carry as their saved playing clip.
    void fullPresetCommitted();

    // A layer's strip mute changed some way other than a pad (on-screen strip, host automation, MIDI learn):
    // the lamps that flash for a muted layer must follow. Message thread; the owner marshals from any thread.
    void notifyMuteChanged() { changed(); }

    // The owner calls this after every deferred-work pass (message thread): settles the queue when the
    // staged preset was superseded by another load, or applied early without a commit hook.
    void reconcile();

    // True while there is something for a periodic reconcile to settle: the mode is on, or a clip plays or waits.
    // The owner's poll checks it first, so an idle instance (a plugin, or a processor still being constructed)
    // never touches its layers from the timer.
    bool needsSettling() const noexcept;

    // ── The clip bank's live half (the owner calls these; message thread) ──
    // The layer's current state was just stored as clip c: that clip is now the one it plays (a launch of it still
    // waiting is dropped). The mask arrives separately, through setClipMask.
    void clipStored(int layer, int clip);
    // Layer n's stored-clip mask changed (store, clear, load, structural edit).
    void setClipMask(int layer, uint8_t mask);
    // A layer preset that is not a clip was staged on, or the layer was added / reset: it plays no clip now.
    void clearLayerClips(int layer);
    // Clip c of layer n was cleared: if it was playing nothing plays; if it was queued the staged launch is cancelled.
    void clipCleared(int layer, int clip);
    // Layers were removed or swapped: playing clips and masks follow their layer (`oldToNew` gives the new index,
    // or -1 when the layer is gone); a moved layer's queued launch is dropped (the edit cancelled it).
    void remapLayers(const std::function<int(int)>& oldToNew);

    // ── Persistence (host session): the performance path, current item and mode ──
    // Not the selected layer, the queue or any clip state. Not in the preset or the APVTS.
    void saveTo(juce::ValueTree& state) const;
    void restoreFrom(const juce::ValueTree& state);   // reloads the file; the mode comes back only if it loads
    static bool modeSaved(const juce::ValueTree& state);   // did this session leave Performance mode on?

private:
    bool setMode(bool on, int seedLayer);
    void clampSelectedLayer();
    bool selectItem(int item);
    bool stepItem(int direction);
    bool itemLoadable(int item) const;
    void changed();

    struct ClipState
    {
        int      playing      = -1;   // -1 = none
        int      queued       = -1;   // -1 = nothing waiting
        uint32_t queuedSerial = 0;    // the layer's stage serial when the queued clip was staged
        uint8_t  mask         = 0;    // bit n = clip n is stored
    };
    static bool inRange(int layer) noexcept { return layer >= 0 && layer < mu_limits::kMaxLayers; }
    bool startClip(int layer, int clip);
    bool stepClip(int direction);
    void setPlaying(int layer, int clip);
    bool dropClipState();           // forgets every layer's playing and queued clip; true when something was set
    bool seedClipsFromLayers();     // each layer's saved playing clip becomes its playing clip; true when any did
    bool resetClips();              // drop, then seed: the state after the layers were replaced
    void syncFullCommits();         // takes the current full-commit count as already accounted for
    void reconcileItem();
    void cancelQueuedItem();      // drops the staged full preset of a queued item
    void reconcileClips();

    Callbacks              cb;
    mu_pp::PerformanceFile performance;
    juce::File             performanceFile;
    bool                   performanceMode = false;
    int                    currentItem     = -1;
    int                    queuedItem      = -1;
    int                    selectedLayer   = 0;
    uint32_t               queuedSerial    = 0;   // the stage serial when the queued item was staged
    uint32_t               revision        = 0;
    std::array<ClipState, (size_t) mu_limits::kMaxLayers> clipState {};
    uint32_t               seenFullCommits = 0;   // the full-preset commit count the clip state was last settled against
    mutable juce::ListenerList<Listener> listeners;
};

} // namespace mu_core
