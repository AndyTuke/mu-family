#pragma once

#include "Plugin/ProcessorBase.h"
#include "Sequencer/LayerHostView.h"

// LayerHostView over a ProcessorBase: the one adapter every host of a layer library's host class uses.
namespace mu_core
{
    class ProcessorLayerHostView : public LayerHostView
    {
    public:
        explicit ProcessorLayerHostView(ProcessorBase& p) : processor(p) {}

        Layer* hostLayer(int slot) noexcept override { return processor.getLayer(slot); }
        juce::AudioProcessorValueTreeState& hostParameters() noexcept override { return processor.apvts; }
        bool   hostTransportPlaying() const noexcept override { return processor.isInternalPlaying(); }
        double hostTransportBeat() const noexcept override    { return processor.getInternalBeatPos(); }
        double hostTempo() const noexcept override            { return processor.getInternalBpm(); }
        int    hostColourIndex(int slot) const override       { return processor.getLayerColourIndex(slot); }
        juce::File hostContentDir() const override            { return processor.getContentDir(); }
        juce::PropertiesFile* hostSettings() noexcept override { return processor.getAppSettingsFile(); }

    private:
        ProcessorBase& processor;
    };
}
