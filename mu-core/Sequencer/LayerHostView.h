#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

struct Layer;

// What a layer library's host adapter reads from the processor around its layers, without naming the processor (a layer library
// never includes Plugin/): the slot's layer, the parameter tree, the internal transport, the slot's colour, the content folder
// and the app settings. mu-core's ProcessorLayerHostView (Plugin/) implements it over ProcessorBase. Message thread.
namespace mu_core
{
    class LayerHostView
    {
    public:
        virtual ~LayerHostView() = default;

        virtual Layer* hostLayer(int slot) noexcept = 0;   // null for an empty or unknown slot
        virtual juce::AudioProcessorValueTreeState& hostParameters() noexcept = 0;
        virtual bool   hostTransportPlaying() const noexcept = 0;
        virtual double hostTransportBeat() const noexcept = 0;
        virtual double hostTempo() const noexcept = 0;
        virtual int    hostColourIndex(int slot) const = 0;
        virtual juce::File hostContentDir() const = 0;
        virtual juce::PropertiesFile* hostSettings() noexcept = 0;   // null when the app keeps none
    };
}
