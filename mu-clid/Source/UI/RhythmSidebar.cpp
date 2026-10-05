#include "RhythmSidebar.h"
#include "Plugin/PluginProcessor.h"

RhythmSidebar::RhythmSidebar(PluginProcessor& p)
    : ChannelSidebar(p, "Rhythm"), proc(p)
{
    // Per-layer mini-graphic = a RhythmCircle bound to the rhythm; its hit edge
    // pulses the parent item.
    createMiniVisual = [this](int i) -> std::unique_ptr<juce::Component>
    {
        auto mv = std::make_unique<RhythmMiniVisual>(proc, i);
        mv->onHit = [this, i] { pulseItem(i); };
        return mv;
    };

    // mu-clid reorder + hot-swap semantics (mu-tant leaves these null).
    onSwapChannels      = [this](int a, int b) { proc.swapRhythms(a, b); };
    isPendingSwap       = [this](int i)        { return proc.hasPendingSwap(i); };
    onCancelPendingSwap = [this](int i)        { proc.cancelStagedSwap(i); };

    refreshItems();
}
