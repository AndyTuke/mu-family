#pragma once
#include "UI/ChannelSidebar.h"
#include "RhythmMiniVisual.h"

class PluginProcessor;

// mu-clid's left sidebar IS the shared mu-core ChannelSidebar, configured with a
// RhythmCircle per-layer mini-graphic + mu-clid's hot-swap / reorder semantics.
// Keeps the historical public callback names (onRhythmSelected / onAddRhythm /
// onRhythmsReordered) as aliases so PluginEditor is unchanged.
class RhythmSidebar : public ChannelSidebar
{
public:
    explicit RhythmSidebar(PluginProcessor& p);

    // Back-compat aliases onto the generic ChannelSidebar callbacks.
    std::function<void(int)>& onRhythmSelected   = onChannelSelected;
    std::function<void()>&    onAddRhythm        = onAddChannel;
    std::function<void(int)>& onRhythmsReordered = onChannelsReordered;

private:
    PluginProcessor& proc;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RhythmSidebar)
};
