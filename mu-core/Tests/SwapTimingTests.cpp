// The family's per-preset swap timing (mu_hotswap::SwapTiming): where a staged swap lands. Guards the
// saved integer order, the free-running fallbacks (a swap must never wait for a master wrap that never
// comes) and the bar rule that mu-Toni / mu-On use.

#include <juce_core/juce_core.h>
#include "Plugin/HotSwap.h"

class SwapTimingTests : public juce::UnitTest
{
public:
    SwapTimingTests() : juce::UnitTest("Swap timing", "SwapTiming") {}

    void runTest() override
    {
        using namespace mu_hotswap;

        beginTest("the saved integers are fixed and an unknown index reads as OnMasterLoop");
        {
            expectEquals((int) SwapTiming::OnMasterLoop, 0);
            expectEquals((int) SwapTiming::OnLayerLoop, 1);
            expectEquals((int) SwapTiming::OnBar, 2);
            expect(swapTimingFromIndex(1) == SwapTiming::OnLayerLoop);
            expect(swapTimingFromIndex(2) == SwapTiming::OnBar);
            expect(swapTimingFromIndex(-1) == SwapTiming::OnMasterLoop);
            expect(swapTimingFromIndex(kNumSwapTimings) == SwapTiming::OnMasterLoop);
        }

        beginTest("a layer swap: OnMasterLoop waits on the master wrap when a master loop is set");
        {
            const auto t = SwapTiming::OnMasterLoop;
            expect(  layerBoundaryReached(t, true, 2, /*master*/ true,  /*mask*/ 0,      /*bar*/ false));
            expect(! layerBoundaryReached(t, true, 2, false,            0b100,           false), "its own wrap does not count");
            expect(  usesMasterLoop(t, true));
        }

        beginTest("a layer swap: with no master loop (free-running) OnMasterLoop uses the layer's own loop");
        {
            const auto t = SwapTiming::OnMasterLoop;
            expect(! usesMasterLoop(t, false));
            expect(  layerBoundaryReached(t, false, 2, false, 0b100, false));
            expect(! layerBoundaryReached(t, false, 2, false, 0b010, false), "another layer's wrap is not this layer's");
            expect(! layerBoundaryReached(t, false, 2, true,  0,     false), "no master wrap is awaited");
        }

        beginTest("a layer swap: OnLayerLoop uses the layer's own loop even with a master loop set");
        {
            const auto t = SwapTiming::OnLayerLoop;
            expect(! usesMasterLoop(t, true));
            expect(  layerBoundaryReached(t, true, 0, false, 0b001, false));
            expect(! layerBoundaryReached(t, true, 0, true,  0,     false));
        }

        beginTest("a layer swap: OnBar lands on the bar line whatever the loops do");
        {
            const auto t = SwapTiming::OnBar;
            expect(  layerBoundaryReached(t, true,  1, false, 0,     true));
            expect(  layerBoundaryReached(t, false, 1, false, 0,     true));
            expect(! layerBoundaryReached(t, true,  1, true,  0b010, false), "a loop wrap is not the bar line");
        }

        beginTest("a layer swap: every layer index 0..7 reads its own bit, and an index outside the mask never fires");
        {
            for (int layer = 0; layer < 8; ++layer)
                for (int other = 0; other < 8; ++other)
                    expect(layerBoundaryReached(SwapTiming::OnLayerLoop, true, layer, false, 1 << other, false) == (layer == other),
                           "layer " + juce::String(layer) + " mask bit " + juce::String(other));
            expect(! layerBoundaryReached(SwapTiming::OnLayerLoop, true, kFullPreset, false, -1, false), "the full-preset sentinel is not a layer");
            expect(! layerBoundaryReached(SwapTiming::OnLayerLoop, true, 40, false, -1, false), "an index past the mask never fires");
        }

        beginTest("a full-preset swap: the master loop when set, else layer 0's loop; OnBar the bar line");
        {
            for (const auto t : { SwapTiming::OnMasterLoop, SwapTiming::OnLayerLoop })
            {
                expect(  fullPresetBoundaryReached(t, true,  true,  0,     false));
                expect(! fullPresetBoundaryReached(t, true,  false, 0b001, false), "master loop set: layer 0's wrap is not enough");
                expect(  fullPresetBoundaryReached(t, false, false, 0b001, false), "free-running: layer 0's wrap stands in");
                expect(! fullPresetBoundaryReached(t, false, false, 0b010, false), "free-running: layer 1's wrap does not");
            }
            expect(  fullPresetBoundaryReached(SwapTiming::OnBar, true,  false, 0, true));
            expect(! fullPresetBoundaryReached(SwapTiming::OnBar, false, true,  1, false));
        }

        beginTest("the bar rule: a bar wraps every four beats, including across a block");
        {
            expect(  loopWrapped(3.9, 4.1, kBarBeats));
            expect(! loopWrapped(4.1, 5.0, kBarBeats));
            expect(  loopWrapped(7.0, 8.0, kBarBeats));
        }
    }
};

static SwapTimingTests swapTimingTests;
