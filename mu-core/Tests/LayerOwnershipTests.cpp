#include "TestProcessorCore.h"

namespace
{
    // A derived layer that counts its destructions, to prove who releases a layer and when.
    struct CountedLayer : Layer
    {
        explicit CountedLayer(int& destroyed) : counter(destroyed) {}
        ~CountedLayer() override { ++counter; }
        int& counter;
    };
}

// ProcessorCore's layer storage: construct, publish, typed access, and destruction off the lock.
struct LayerOwnershipTests : juce::UnitTest
{
    LayerOwnershipTests() : juce::UnitTest("ProcessorCore layer ownership", "mu-core") {}

    void runTest() override
    {
        beginTest("layerAs returns the derived layer, including slots above the live count");
        {
            int destroyed = 0;
            TestProcessorCore p(1);
            p.layers[5] = std::make_unique<CountedLayer>(destroyed);
            expect(p.getLayer(5) == nullptr, "a slot above the count is not live");
            expect(&p.layerAs<CountedLayer>(5) == p.layers[5].get());
            expect(&p.layerAs<Layer>(0) == p.getLayer(0));
        }

        beginTest("publishing a layer makes it visible through the count");
        {
            int destroyed = 0;
            TestProcessorCore p(2);
            p.layers[2] = std::make_unique<CountedLayer>(destroyed);
            expect(p.getLayer(2) == nullptr);
            {
                const juce::ScopedLock sl(p.layersLock);
                p.numLayers.store(3, std::memory_order_release);
            }
            expectEquals(p.getNumChannels(), 3);
            expect(p.getLayer(2) == p.layers[2].get());
        }

        beginTest("a removed layer is destroyed after the lock is released, not under it");
        {
            int destroyed = 0;
            TestProcessorCore p(1);
            p.layers[1] = std::make_unique<CountedLayer>(destroyed);
            p.numLayers.store(2);

            std::unique_ptr<Layer> removed;
            {
                const juce::ScopedLock sl(p.layersLock);
                removed = std::move(p.layers[1]);
                p.numLayers.store(1, std::memory_order_release);
            }
            expectEquals(destroyed, 0, "still alive while held outside the lock");
            expect(p.getLayer(1) == nullptr);
            removed.reset();
            expectEquals(destroyed, 1);
        }

        beginTest("the processor destroys the layers it still owns");
        {
            int destroyed = 0;
            {
                TestProcessorCore p(0);
                for (int i = 0; i < 4; ++i) p.layers[(size_t) i] = std::make_unique<CountedLayer>(destroyed);
            }
            expectEquals(destroyed, 4);
        }
    }
};

static LayerOwnershipTests layerOwnershipTests;
