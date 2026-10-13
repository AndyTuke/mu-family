#pragma once

#include "Control/ControlAction.h"
#include "Control/LiveView.h"
#include <juce_data_structures/juce_data_structures.h>
#include <functional>

// The live state behind Performance mode (docs/design-plugin-family.md, "Performance mode and live
// state"): the loaded performance, which item plays and which is queued, the selected layer. Owned by
// the instance (ProcessorBase), so a pad press needs no window; plain class, no processor dependency,
// MESSAGE THREAD ONLY (no atomics, no locks; the audio thread never touches it).
//
// It performs the actions that change that state (PerformanceMode / Item / Prev / Next, SelectLayer).
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
    void addListener(Listener* l) const override    { listeners.add(l); }
    void removeListener(Listener* l) const override { listeners.remove(l); }

    // ── Loading a performance (a screen-only call, made before the mode is entered) ──
    // Replaces the loaded list and forgets which item played. Refused while the mode is on.
    bool loadPerformance(mu_pp::PerformanceFile file, const juce::File& where);

    // ── Actions ──────────────────────────────────────────────────────────────
    // True for the types this class owns: PerformanceMode, PerformanceItem, PerformancePrev, PerformanceNext, SelectLayer.
    static bool handles(ControlActionType t) noexcept;

    // Performs one of those. Every one but PerformanceMode returns false while the mode is off.
    // PerformanceMode: `index` 1 = on, 0 = off, -1 (the default) = toggle; refused when the product does not
    // support the mode or no performance is loaded; turning it on takes `layer` as the selected layer.
    bool perform(const ControlAction& action);

    // The owner's "a full-preset swap just committed" hook (message thread): the queued item becomes
    // current when the preset that landed is the one this class staged; a preset loaded some other way
    // (browser, MIDI program change) leaves no item current.
    void fullPresetCommitted();

    // The owner calls this after every deferred-work pass (message thread): settles the queue when the
    // staged preset was superseded by another load, or applied early without a commit hook.
    void reconcile();

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

    Callbacks              cb;
    mu_pp::PerformanceFile performance;
    juce::File             performanceFile;
    bool                   performanceMode = false;
    int                    currentItem     = -1;
    int                    queuedItem      = -1;
    int                    selectedLayer   = 0;
    uint32_t               queuedSerial    = 0;   // the stage serial when the queued item was staged
    uint32_t               revision        = 0;
    mutable juce::ListenerList<Listener> listeners;
};

} // namespace mu_core
