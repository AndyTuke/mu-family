#include "TestProcessorBase.h"

// A host bypass calls processBlockBypassed instead of processBlock, so no block flags the hot-swap boundaries: the
// swapper must forget the last transport state, or a preset loaded while bypassed would stage for a boundary nobody
// flags and only land after un-bypass.
struct BypassSwapTests : juce::UnitTest
{
    BypassSwapTests() : juce::UnitTest("ProcessorBase bypass and hot swap", "mu-core") {}

    // Exposes the swapper's resolved play state, and lets the test stand in for a playing block.
    struct Probe : TestProcessorBase
    {
        Probe() : TestProcessorBase(juce::AudioProcessor::wrapperType_Undefined) {}
        void playingBlock()           { flagSwapBoundaries(true, 0.0, 1.0); }
        bool swapperBelievesPlaying() { return layerSwapper.isRunning(); }
    };

    void runTest() override
    {
        beginTest("a bypassed block makes the swapper forget a running transport, so a load applies at once");
        {
            Probe p;
            p.prepareToPlay(48000.0, 64);
            p.playingBlock();
            expect(p.swapperBelievesPlaying(), "precondition: a playing block was seen");

            juce::AudioBuffer<float> buffer(2, 64);
            buffer.clear();
            juce::MidiBuffer midi;
            p.processBlockBypassed(buffer, midi);

            expect(! p.swapperBelievesPlaying(), "after a bypassed block the swapper must not stage for a boundary");
        }

        beginTest("a playing block after the bypass restores the play state (nothing sticks)");
        {
            Probe p;
            p.prepareToPlay(48000.0, 64);
            juce::AudioBuffer<float> buffer(2, 64);
            buffer.clear();
            juce::MidiBuffer midi;
            p.processBlockBypassed(buffer, midi);
            expect(! p.swapperBelievesPlaying());
            p.playingBlock();
            expect(p.swapperBelievesPlaying());
        }
    }
};

static BypassSwapTests bypassSwapTests;
