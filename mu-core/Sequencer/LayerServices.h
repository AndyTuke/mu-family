#pragma once

// What a layer type shares between its layers (docs/mu-tronic/design.md 2.4): a wavetable bank, a sample library,
// anything that belongs to the type and not to one layer. The processor owns at most one per layer type, built by the
// type's LayerTypeInfo::createServices when the first layer of that type is added, and keeps it until the processor
// is destroyed, after every layer and every retired tail (ProcessorCore declares the storage before the layers).
// The type's host adapter reads it; the base only creates, prepares and destroys it, so it stays opaque here.
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
    };
}
