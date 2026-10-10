#pragma once

#include "Sequencer/Layer.h"   // mu-core: name, colour, modulation

namespace mu_tant
{

class PluginProcessor;

// One mu-Tant layer (a drone voice): the shared Layer plus the hooks that save and load its
// non-parameter data. The per-voice gate / filter / pitch patterns and engine state still live in
// the processor, so the hooks hand over to it; they move in here as the layer plan progresses.
struct Pattern : Layer
{
    OwnerLink<PluginProcessor> owner;
    void writeExtras(juce::ValueTree& node) const override;
    void applyExtras(const juce::ValueTree& node) override;
};

} // namespace mu_tant
