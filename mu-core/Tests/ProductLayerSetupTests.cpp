#include "TestProcessorCore.h"

// The layer setup the products with a fixed layer count use: slots allocated in the base-owned
// storage in the constructor, the live count published, and the base getNumChannels / getLayer
// reading them. The real mu-On and mu-Toni processors cannot be built in a unit target (their
// createEditor pulls in the whole editor tree), so these mirror their constructors on a
// TestProcessorCore: same counts, same calls, same base accessors.
struct ProductLayerSetupTests : juce::UnitTest
{
    ProductLayerSetupTests() : juce::UnitTest("Product layer setup", "mu-core") {}

    // A product's constructor in miniature: allocate `allocated` slots, publish `live` of them.
    struct FixedCountProcessor : TestProcessorCore
    {
        FixedCountProcessor(int allocated, int live) : TestProcessorCore(0)
        {
            for (int i = 0; i < allocated; ++i) layers[(size_t) i] = std::make_unique<Layer>();
            numLayers.store(live, std::memory_order_relaxed);
        }
    };

    void runTest() override
    {
        // mu-On: five lanes (Kick / Bass / Hat / Snare / Rumble), all live, none spare.
        beginTest("fixed five-layer product: every live layer is non-null and typed access matches");
        {
            FixedCountProcessor p(5, 5);
            expectEquals(p.getNumChannels(), 5);
            for (int i = 0; i < 5; ++i)
            {
                expect(p.getLayer(i) != nullptr, "layer " + juce::String(i) + " is null");
                expect(p.getLayer(i) == &p.layerAs<Layer>(i), "layerAs and getLayer disagree at " + juce::String(i));
            }
            expect(p.getLayer(5) == nullptr, "a slot past the live count must read as null");
            expect(p.getLayer(-1) == nullptr, "a negative index must read as null");
        }

        // mu-Toni: eight allocated arp slots, four live; the spares exist but are not live.
        beginTest("eight allocated slots, four live: live ones read through getLayer, spares do not");
        {
            FixedCountProcessor p(8, 4);
            expectEquals(p.getNumChannels(), 4);
            for (int i = 0; i < 8; ++i)
                expect(p.layers[(size_t) i] != nullptr, "slot " + juce::String(i) + " was not allocated");
            for (int i = 0; i < 4; ++i)
                expect(p.getLayer(i) == p.layers[(size_t) i].get(), "getLayer(" + juce::String(i) + ") is not the slot's layer");
            for (int i = 4; i < 8; ++i)
                expect(p.getLayer(i) == nullptr, "spare slot " + juce::String(i) + " must not be live");
        }
    }
};

static ProductLayerSetupTests productLayerSetupTests;
