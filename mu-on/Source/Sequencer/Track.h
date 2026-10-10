#pragma once

#include "Sequencer/Layer.h"   // mu-core: per-lane ControlSequences + ModulationMatrix

#include <atomic>

namespace mu_on
{

class PluginProcessor;

// One mu-On lane (Kick / Bass / Hat / Snare / Rumble): the shared Layer (name, colour, modulation)
// plus the trigger counter the audio thread bumps and the editor polls to pulse the sidebar lane.
struct Track : Layer
{
    std::atomic<int> triggers { 0 };

    // The step row and Rumble envelope still live in the processor, so the hooks hand over to it.
    OwnerLink<PluginProcessor> owner;
    void writeExtras(juce::ValueTree& node) const override;
    void applyExtras(const juce::ValueTree& node) override;
    bool isValidDest(const std::string& id) const override;
};

} // namespace mu_on
