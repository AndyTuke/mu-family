#include "TestProcessorCore.h"

// ProcessorCore's layer accessors: a valid index returns the layer, anything else nullptr.
struct LayerAccessTests : juce::UnitTest
{
    LayerAccessTests() : juce::UnitTest("ProcessorCore layer access", "mu-core") {}

    void runTest() override
    {
        beginTest("a valid index returns that layer");
        {
            TestProcessorCore p;
            for (int i = 0; i < p.getNumChannels(); ++i)
                expect(p.getLayer(i) == p.layers[(size_t) i].get());
        }

        beginTest("out-of-range indices return nullptr");
        {
            TestProcessorCore p;
            expect(p.getLayer(-1) == nullptr);
            expect(p.getLayer(p.getNumChannels()) == nullptr);
        }

        beginTest("the const overload returns the same layer");
        {
            TestProcessorCore p;
            const TestProcessorCore& cp = p;
            expect(cp.getLayer(1) == p.getLayer(1));
            expect(cp.getLayer(99) == nullptr);
        }

        beginTest("name and colour set on a layer are read back through the base accessors");
        {
            TestProcessorCore p;
            p.getLayer(2)->name = "Snare";
            p.getLayer(2)->colourIndex = 5;
            expectEquals(p.getChannelName(2), juce::String("Snare"));
            expectEquals(p.getChannelColourIndex(2), 5);
        }

        beginTest("a layer reports no insert gain-reduction meter unless it overrides it");
        {
            TestProcessorCore p;
            expect(p.getLayer(0)->insertGainReduction() == nullptr);
        }

        beginTest("a SlotBound member is never copied or assigned between layers");
        {
            SlotBound<int> a, b;
            a.value = 7;
            b.value = 3;
            a = b;                                  // assignment leaves the destination alone
            expectEquals(a.value, 7);
            SlotBound<int> c(a);                    // a copy starts with a default member
            expectEquals(c.value, 0);
            *c = 5;
            expectEquals(c.value, 5);
        }
    }
};

static LayerAccessTests layerAccessTests;
