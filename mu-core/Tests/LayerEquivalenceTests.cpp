// The equivalence harness (LayerEquivalence.h) proven on the fake host: its old processor-side chain and the
// shared driver must match bit for bit at the master and at every channel's pre-mixer stream, and a
// deliberate difference must be caught. extraction-plan 2.7a; tests.md section 3.

#include "LayerContractFixtures.h"
#include "LayerEquivalence.h"

using namespace mu_contract_test;

struct LayerEquivalenceTests : juce::UnitTest
{
    LayerEquivalenceTests() : juce::UnitTest("Layer equivalence harness (useLayerRender, channel tap)", "mu-core") {}

    void runTest() override
    {
        mu_golden::Scenario sc;
        sc.name = "fake-equivalence";
        sc.lengthFrames = 48000;
        sc.startBeat = 2.0;
        // Mid-render edits applied to both processors: a parameter move, a stop and a restart elsewhere.
        sc.events.push_back({ 20, "gain", [](mu_golden::ScriptContext& c) { c.proc.apvts.getParameter("v1_a")->setValueNotifyingHost(0.9f); } });
        sc.events.push_back({ 40, "stop", [](mu_golden::ScriptContext& c) { c.host.playing = false; } });
        sc.events.push_back({ 44, "locate", [](mu_golden::ScriptContext& c) { c.host.playing = true; c.host.ppq = 33.0; } });
        const mu_golden::ProcessorFactory factory = [] { return std::make_unique<FakeLayerHost>(3); };

        beginTest("The old chain and the shared driver are bit-identical at the master and every channel (fixed 512)");
        {
            const auto r = mu_layer_equiv::compare(factory, sc, mu_golden::Schedule::fixed512(), 3);
            expect(r.ok, r.message);
            expect(r.blocks > 90);
        }

        beginTest("... and on a ragged schedule within the prepared size (1, 7, 63, 64, 129, 512, 3)");
        {
            const mu_golden::Schedule ragged { "ragged-in-size", { 1, 7, 63, 64, 129, 512, 3 } };
            const auto r = mu_layer_equiv::compare(factory, sc, ragged, 3);
            expect(r.ok, r.message);
        }

        beginTest("A real difference on the layer path is caught, with the block and frame it starts at");
        {
            struct Skewed : FakeLayerHost
            {
                Skewed() : FakeLayerHost(3) {}
                void processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer& m) override
                {
                    // The layer path only: slot 2 reads slot 0's gain (a stale binding, the bug C9 guards against).
                    if (usesLayerRender()) fake(2).gainParam = mu_core::ParamBinding(apvts.getRawParameterValue("v0_a"), &fake(2));
                    FakeLayerHost::processBlock(b, m);
                }
            };
            mu_golden::Scenario s2 = sc;
            s2.events.clear();
            s2.loadState = [](ProcessorBase& p) { p.apvts.getParameter("v0_a")->setValueNotifyingHost(0.8f); };
            const mu_golden::ProcessorFactory skewed = [] { return std::make_unique<Skewed>(); };
            const auto r = mu_layer_equiv::compare(skewed, s2, mu_golden::Schedule::fixed512(), 3);
            expect(! r.ok, "the harness must not pass a changed render");
            logMessage("caught: " + r.message);
        }

        beginTest("CPU (C18 groundwork, information only): the shared driver against the old chain, every slot live");
        {
            // Load-sensitive: the ratio is logged, never asserted (tests.md section 6.7 sets the budget later).
            auto time = [](bool layerPath, int block)
            {
                FakeLayerHost p(mu_limits::kMaxLayers);
                p.setUseLayerRenderForTest(layerPath);
                Driver d(p);
                for (int i = 0; i < 50; ++i) d.block(block);   // warm up
                const auto t0 = juce::Time::getHighResolutionTicks();
                for (int i = 0; i < 1000; ++i) d.block(block);
                return juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - t0);
            };
            for (int block : { 64, 128, 512 })
            {
                const double oldPath = time(false, block), layerPath = time(true, block);
                logMessage("cpu: " + juce::String(mu_limits::kMaxLayers) + " fake layers, block " + juce::String(block)
                           + ": old chain " + juce::String(oldPath * 1000.0, 2) + " ms, shared driver "
                           + juce::String(layerPath * 1000.0, 2) + " ms per 1000 blocks, ratio "
                           + juce::String(oldPath > 0.0 ? layerPath / oldPath : 0.0, 3) + " (information)");
            }
        }
    }
};
static LayerEquivalenceTests sLayerEquivalenceTests;
