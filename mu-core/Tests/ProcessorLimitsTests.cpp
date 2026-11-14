// The runtime-count types (docs/design-runtime-counts.md): FlatArray and ProcessorLimits, plus ProcessorBase keeping what it is given.
#include "Audio/FlatArray.h"
#include "MuLimits.h"
#include "TestProcessorBase.h"

#include <atomic>
#include <juce_core/juce_core.h>

struct ProcessorLimitsTests : juce::UnitTest
{
    ProcessorLimitsTests() : juce::UnitTest("ProcessorLimits and FlatArray", "mu-core") {}

    void runTest() override
    {
        using mu_limits::ProcessorLimits;

        beginTest("FlatArray: sized once, elements default-constructed, indexed and iterated");
        {
            mu_core::FlatArray<int> a(5);
            expectEquals(a.size(), 5);
            expect(! a.empty());
            for (int i = 0; i < a.size(); ++i) expectEquals(a[i], 0);
            for (int i = 0; i < a.size(); ++i) a[i] = i * 3;
            int sum = 0;
            for (int v : a) sum += v;
            expectEquals(sum, 30);
            expect(a.data() == &a[0]);
        }

        beginTest("FlatArray: size 0 is empty and safe, reset re-sizes, move keeps the block, atomics are allowed");
        {
            mu_core::FlatArray<int> none(0);
            expect(none.empty());
            expect(none.begin() == none.end());

            mu_core::FlatArray<std::atomic<float>> peaks(4);
            peaks[2].store(0.5f);
            auto* p = peaks.data();
            mu_core::FlatArray<std::atomic<float>> moved(std::move(peaks));
            expect(moved.data() == p);
            expectEquals(moved.size(), 4);
            expectEquals(moved[2].load(), 0.5f);

            moved.reset(2);
            expectEquals(moved.size(), 2);
        }

        beginTest("ProcessorLimits: valid ranges and the ceilings");
        {
            expect(ProcessorLimits {}.valid());
            expect(ProcessorLimits::fromBuildSettings().valid());
            expect((ProcessorLimits { mu_limits::kAbsoluteMaxLayers, mu_limits::kAbsoluteMaxBuses, mu_limits::kAbsoluteBusInserts, false }).valid());
            expect(! (ProcessorLimits { 0, 0, 2, true }).valid());                                        // at least one layer
            expect(! (ProcessorLimits { mu_limits::kAbsoluteMaxLayers + 1, 0, 2, true }).valid());        // layer masks are 32-bit
            expect(! (ProcessorLimits { 8, mu_limits::kAbsoluteMaxBuses + 1, 2, true }).valid());         // one-digit bus ids
            expect(! (ProcessorLimits { 8, 3, 0, true }).valid());                                         // a bus has at least one insert
        }

        beginTest("ProcessorLimits: the build settings are what the macros say");
        {
            const auto l = ProcessorLimits::fromBuildSettings();
            expectEquals(l.layers, mu_limits::kMaxLayers);
            expectEquals(l.buses, mu_limits::kMaxBuses);
            expectEquals(l.busInserts, mu_limits::kBusInserts);
            expect(l.externalSidechain == mu_limits::kExternalSidechain);
        }

        beginTest("ProcessorBase: keeps the limits it is built with (default = the build settings)");
        {
            TestProcessorBase p(juce::AudioProcessor::wrapperType_Undefined);
            expectEquals(p.getLimits().layers, mu_limits::kMaxLayers);
            expectEquals(p.getLimits().buses, mu_limits::kMaxBuses);
        }
    }
};

static ProcessorLimitsTests processorLimitsTests;
