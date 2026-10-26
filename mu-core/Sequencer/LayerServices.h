#pragma once

// What a layer type shares between its layers (docs/mu-tronic/design.md 2.4): a wavetable bank, a sample library,
// anything that belongs to the type and not to one layer. The processor owns at most one per layer type, built by the
// type's LayerTypeInfo::createServices when the first layer of that type is added, and keeps it until the processor
// is destroyed, after every layer and every retired tail (ProcessorCore declares the storage before the layers).
// The type's host adapter reads it; the base only creates, prepares and destroys it, so it stays opaque here.
#include <juce_audio_basics/juce_audio_basics.h>

namespace mu_core
{
    class LayerServices
    {
    public:
        virtual ~LayerServices() = default;

        // Message or host thread, while no block can be rendering a layer that reads the services (prepareToPlay),
        // and once when the services are built after the processor has been prepared: size whatever depends on the
        // sample rate and block size. May allocate. Default: nothing.
        virtual void prepare(double sampleRate, int blockSize) { (void) sampleRate; (void) blockSize; }

        // Message thread, in the processor's deferred-work pass (the pass the audio thread or another thread asked for with
        // ProcessorBase::requestLayerServicesWork): work that a listener on another thread could only queue, such as
        // applying a parameter change to a layer. Default: nothing.
        virtual void deferredWork() {}

        // Message thread, a Published host's swapLayers: slots `a` and `b` have just exchanged their layers (the audio thread is
        // excluded, the layers are not yet re-bound to their new slots). Move whatever the services keep per slot with the
        // layer that used it, so the re-bind finds the moved layer's own state. Default: nothing.
        virtual void slotsSwapped(int a, int b) { (void) a; (void) b; }

        // Audio thread, once per rendered chunk after the mixer (at most the prepared block size): what the type's services
        // add to the master bus themselves, such as a sample browser's preview. No allocation, no lock, no wait. Default:
        // nothing.
        virtual void mixIntoMaster(juce::AudioBuffer<float>& master, int numSamples) noexcept { (void) master; (void) numSamples; }
    };
}
