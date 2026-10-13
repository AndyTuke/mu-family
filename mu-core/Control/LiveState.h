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
    struct Callbacks
    {
        std::function<bool()>     supportsPerformanceMode;   // this product and build allow the mode
        std::function<bool(int)>  canLoad;                   // item n is this product's and its file exists
        std::function<bool(int)>  stageItem;                 // stage item n's full preset; true = applied at once (stopped)
        std::function<void()>     cancelItem;                // drop the staged full preset
        std::function<int()>      layerCount;                // how many layers are live (bounds SelectLayer)
    };

    void setCallbacks(Callbacks c) { cb = std::move(c); }

    // ── LiveView ─────────────────────────────────────────────────────────────
    bool                          isPerformanceMode() const noexcept override { return performanceMode; }
    const mu_pp::PerformanceFile& getPerformance()    const noexcept override { return performance; }
    juce::File                    getPerformanceFile() const override         { return performanceFile; }
    int                           getCurrentItem()    const noexcept override { return currentItem; }
    int                           getQueuedItem()     const noexcept override { return queuedItem; }
    int                           getSelectedLayer()  const noexcept override { return selectedLayer; }
    uint32_t                      getRevision()       const noexcept override { return revision; }
    void addListener(Listener* l) override    { listeners.add(l); }
    void removeListener(Listener* l) override { listeners.remove(l); }

    // ── Loading a performance (a screen-only call, made before the mode is entered) ──
    // Replaces the loaded list and forgets which item played. Refused while the mode is on.
    bool loadPerformance(mu_pp::PerformanceFile file, const juce::File& where);

    // ── Actions ──────────────────────────────────────────────────────────────
    // True for the types this class owns: PerformanceMode, PerformanceItem, PerformancePrev, PerformanceNext, SelectLayer.
    static bool handles(ControlActionType t) noexcept;

    // Performs one of those. Every one but PerformanceMode returns false while the mode is off.
    // PerformanceMode: `value` 1 = on, 0 = off, negative = toggle; refused when the product does not support
    // the mode or no performance is loaded; turning it on takes `layer` as the selected layer.
    bool perform(const ControlAction& action);

    // The owner's "a full-preset swap just committed" hook (message thread): the queued item becomes current.
    void fullPresetCommitted();

    // ── Persistence (host session): the performance path, current item and mode ──
    // Not the selected layer, the queue or any clip state. Not in the preset or the APVTS.
    void saveTo(juce::ValueTree& state) const;
    void restoreFrom(const juce::ValueTree& state);   // reloads the file; the mode comes back only if it loads

private:
    bool setMode(bool on, int seedLayer);
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
    uint32_t               revision        = 0;
    juce::ListenerList<Listener> listeners;
};

} // namespace mu_core
