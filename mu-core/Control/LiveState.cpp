#include "LiveState.h"

namespace mu_core
{

namespace
{
const juce::Identifier kPerformancePath { "performancePath" };
const juce::Identifier kPerformanceItem { "performanceItem" };
const juce::Identifier kPerformanceMode { "performanceMode" };
}

bool LiveState::handles(ControlActionType t) noexcept
{
    return t == ControlActionType::PerformanceMode || t == ControlActionType::PerformanceItem
        || t == ControlActionType::PerformancePrev || t == ControlActionType::PerformanceNext
        || t == ControlActionType::SelectLayer;
}

void LiveState::changed()
{
    ++revision;
    listeners.call([](Listener& l) { l.liveStateChanged(); });
}

bool LiveState::loadPerformance(mu_pp::PerformanceFile file, const juce::File& where)
{
    if (performanceMode) return false;   // the list is fixed while the screen is locked
    if (queuedItem >= 0 && cb.cancelItem) cb.cancelItem();
    performance     = std::move(file);
    performanceFile = where;
    currentItem     = -1;
    queuedItem      = -1;
    changed();
    return true;
}

bool LiveState::itemLoadable(int item) const
{
    return item >= 0 && item < mu_pp::PerformanceFile::kMaxItems && cb.canLoad && cb.canLoad(item);
}

bool LiveState::setMode(bool on, int seedLayer)
{
    if (on == performanceMode) return true;
    if (on)
    {
        if (! (cb.supportsPerformanceMode && cb.supportsPerformanceMode())) return false;
        if (performance.count() == 0) return false;   // nothing to perform
        const int layers = cb.layerCount ? cb.layerCount() : 1;
        selectedLayer = juce::jlimit(0, juce::jmax(0, layers - 1), seedLayer);
    }
    else
    {
        // Leaving the mode drops a queued item: nothing is left to land it, and the lamps would lie.
        if (queuedItem >= 0 && cb.cancelItem) cb.cancelItem();
        queuedItem = -1;
    }
    performanceMode = on;
    changed();
    return true;
}

bool LiveState::selectItem(int item)
{
    if (! itemLoadable(item)) return false;

    if (item == currentItem)
    {
        if (queuedItem < 0) return true;   // already playing it: nothing to do
        if (cb.cancelItem) cb.cancelItem();   // pressing the playing item while another is queued cancels the switch
        queuedItem = -1;
        changed();
        return true;
    }
    if (item == queuedItem) return true;   // already on its way

    const auto result = cb.stageItem ? cb.stageItem(item) : Stage::Failed;
    if (result == Stage::Failed) return false;   // it did not load: nothing is staged, nothing changes
    if (result == Stage::Applied) { currentItem = item; queuedItem = -1; clampSelectedLayer(); }   // the new preset may have fewer layers
    else                          { queuedItem = item; queuedSerial = cb.stageSerial ? cb.stageSerial() : 0; }
    changed();
    return true;
}

bool LiveState::stepItem(int direction)
{
    // Step from where the switch is heading: the queued item if any, else the current one, so two presses
    // before the commit land two items on. Wraps; skips empty, missing and other-product items.
    const int n      = mu_pp::PerformanceFile::kMaxItems;
    const int origin = queuedItem >= 0 ? queuedItem : currentItem;
    for (int i = 1; i <= n; ++i)
    {
        const int candidate = origin < 0 ? (direction > 0 ? i - 1 : n - i)
                                         : ((origin + direction * i) % n + n) % n;
        if (candidate == origin) continue;   // no other loadable item
        if (itemLoadable(candidate)) return selectItem(candidate);
    }
    return false;
}

bool LiveState::perform(const ControlAction& a)
{
    using T = ControlActionType;
    switch (a.type)
    {
        case T::PerformanceMode:
            return setMode(a.index < 0 ? ! performanceMode : a.index != 0, a.layer);   // `index` (a mapped button's value is the message's)
        default: break;
    }

    if (! performanceMode) return false;   // every other action needs the mode

    switch (a.type)
    {
        case T::PerformanceItem: return selectItem(a.index);
        case T::PerformancePrev: return stepItem(-1);
        case T::PerformanceNext: return stepItem(+1);
        case T::SelectLayer:
        {
            const int layers = cb.layerCount ? cb.layerCount() : 1;
            if (a.layer < 0 || a.layer >= layers) return false;
            if (a.layer != selectedLayer) { selectedLayer = a.layer; changed(); }
            return true;
        }
        default: return false;
    }
}

void LiveState::clampSelectedLayer()
{
    const int layers = cb.layerCount ? cb.layerCount() : 1;
    selectedLayer = juce::jlimit(0, juce::jmax(0, layers - 1), selectedLayer);   // a commit can shrink the layer count
}

void LiveState::fullPresetCommitted()
{
    if (queuedItem >= 0) { reconcile(); return; }
    // A preset that was not one of this performance's items took over.
    const int before = selectedLayer;
    clampSelectedLayer();
    if (currentItem >= 0 || selectedLayer != before) { currentItem = -1; changed(); }
}

void LiveState::reconcile()
{
    if (queuedItem < 0) return;
    const bool pending = cb.fullPending && cb.fullPending();
    const bool ours    = cb.stageSerial && cb.stageSerial() == queuedSerial;
    if (pending && ! ours)
    {
        queuedItem = -1;   // another load replaced the staged preset: our item is no longer on its way
        changed();
    }
    else if (! pending)
    {
        currentItem = ours ? queuedItem : -1;   // it landed (or another preset did): settle
        queuedItem  = -1;
        clampSelectedLayer();
        changed();
    }
}

void LiveState::saveTo(juce::ValueTree& state) const
{
    if (performanceFile == juce::File())
    {
        state.removeProperty(kPerformancePath, nullptr);
        state.removeProperty(kPerformanceItem, nullptr);
        state.removeProperty(kPerformanceMode, nullptr);
        return;
    }
    state.setProperty(kPerformancePath, performanceFile.getFullPathName(), nullptr);
    state.setProperty(kPerformanceItem, currentItem, nullptr);
    state.setProperty(kPerformanceMode, performanceMode, nullptr);
}

bool LiveState::modeSaved(const juce::ValueTree& state)
{
    return state.isValid() && (bool) state.getProperty(kPerformanceMode, false);
}

void LiveState::restoreFrom(const juce::ValueTree& state)
{
    if (queuedItem >= 0 && cb.cancelItem) cb.cancelItem();
    performanceMode = false;
    currentItem = queuedItem = -1;
    performance = {};
    performanceFile = {};

    const juce::File file(state.getProperty(kPerformancePath).toString());
    if (file != juce::File())
    {
        mu_pp::PerformanceFile loaded;
        if (mu_pp::PerformanceFile::load(file, loaded) == mu_pp::PerformanceFile::LoadResult::Ok)
        {
            performance     = std::move(loaded);
            performanceFile = file;
            const int item  = (int) state.getProperty(kPerformanceItem, -1);
            currentItem     = (item >= 0 && item < mu_pp::PerformanceFile::kMaxItems) ? item : -1;
            // The mode comes back only if this build still supports it and the list is usable.
            if ((bool) state.getProperty(kPerformanceMode, false)
                && cb.supportsPerformanceMode && cb.supportsPerformanceMode() && performance.count() > 0)
                performanceMode = true;
        }
    }
    changed();
}

} // namespace mu_core
