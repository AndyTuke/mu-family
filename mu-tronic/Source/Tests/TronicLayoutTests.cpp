// The 16-slot parameter layout: what the real layout holds, what a layout the size the finished product will have
// costs to build, and that the strips of all sixteen slots are bound.
//
// The cost check is docs/mu-tronic/design.md 3.4: every slot will declare every layer type's parameters, about
// 4,000 to 6,000 in all, and the editor must open in under a second. The numbers below are measured here and
// logged; the budget they are held to is generous (the log line is the record).

#include "Plugin/PluginProcessor.h"
#include "Tests/ProcessorSeamHelpers.h"

#include <chrono>
#include <set>

#if JUCE_WINDOWS
 #include <windows.h>
 #include <psapi.h>
 #pragma comment(lib, "psapi.lib")
#endif

namespace
{
    // The process's private memory in bytes (0 where it cannot be read).
    juce::int64 privateBytes()
    {
       #if JUCE_WINDOWS
        PROCESS_MEMORY_COUNTERS_EX info {};
        if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&info), sizeof(info)))
            return (juce::int64) info.PrivateUsage;
       #endif
        return 0;
    }

    // A layout of `perSlot` float parameters for each of the 16 slots (the shape of the finished product's, by count).
    juce::AudioProcessorValueTreeState::ParameterLayout syntheticLayout(int perSlot)
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        for (int s = 0; s < mu_limits::kMaxLayers; ++s)
            for (int p = 0; p < perSlot; ++p)
                layout.add(std::make_unique<juce::AudioParameterFloat>(
                    juce::ParameterID { "x" + juce::String(s) + "_p" + juce::String(p), 1 }, "Parameter " + juce::String(p),
                    juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
        return layout;
    }

    struct SilentProcessor : juce::AudioProcessor
    {
        SilentProcessor() : juce::AudioProcessor(BusesProperties().withOutput("Out", juce::AudioChannelSet::stereo())) {}
        const juce::String getName() const override { return "layout-probe"; }
        void prepareToPlay(double, int) override {}
        void releaseResources() override {}
        void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        double getTailLengthSeconds() const override { return 0.0; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram(int) override {}
        const juce::String getProgramName(int) override { return {}; }
        void changeProgramName(int, const juce::String&) override {}
        void getStateInformation(juce::MemoryBlock&) override {}
        void setStateInformation(const void*, int) override {}
    };
}

struct MuTronicLayoutTests : juce::UnitTest
{
    MuTronicLayoutTests() : juce::UnitTest("mu-Tronic 16-slot parameter layout", "mu-tronic-layout") {}

    void runTest() override
    {
        beginTest("the real layout declares a full mixer strip for each of the sixteen slots, with unique ids");
        {
            mu_seam_test::TempStorage storage;
            mu_tronic::PluginProcessor p;
            std::set<juce::String> ids;
            for (auto* raw : p.getParameters())
                if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(raw))
                    expect(ids.insert(withId->getParameterID()).second, "duplicate parameter id " + withId->getParameterID());
            for (int s = 0; s < 16; ++s)
                for (const char* suffix : { "lvl", "pan", "mute", "solo", "sendEff", "sendDly", "sendRev", "scSrc", "scAmt", "scAtk", "scRel", "outBus" })
                    expect(p.apvts.getParameter("ch" + juce::String(s) + "_" + suffix) != nullptr, "ch" + juce::String(s) + "_" + suffix);
            expect(p.apvts.getParameter("ch16_lvl") == nullptr, "no seventeenth strip");
            logMessage("mu-Tronic parameters today: " + juce::String((int) ids.size()));
        }

        beginTest("a strip's sidechain source reaches every slot and the external bus");
        {
            mu_seam_test::TempStorage storage;
            mu_tronic::PluginProcessor p;
            auto* sc = dynamic_cast<juce::AudioParameterInt*>(p.apvts.getParameter("ch0_scSrc"));
            expect(sc != nullptr);
            if (sc != nullptr) expectEquals(sc->getNormalisableRange().end, (float) (mu_limits::kMaxLayers + 1 + mu_limits::kMaxBuses), "0 off, 1..16 slots, 17 external, 18.. the buses");
            auto* ret = dynamic_cast<juce::AudioParameterInt*>(p.apvts.getParameter("ret_eff_scSrc"));
            expect(ret != nullptr);
            if (ret != nullptr) expectEquals(ret->getNormalisableRange().end, (float) (mu_limits::kMaxLayers + 1 + mu_limits::kMaxBuses));
        }

        beginTest("cost check: a 16-slot layout of the finished product's size builds well inside the budget");
        {
            // The design's estimate is 4,000 to 6,000 parameters; take the top of it.
            constexpr int kPerSlot = 6000 / 16;
            SilentProcessor host;
            const auto memBefore = privateBytes();
            const auto t0 = std::chrono::steady_clock::now();
            auto apvts = std::make_unique<juce::AudioProcessorValueTreeState>(host, nullptr, juce::Identifier("Probe"), syntheticLayout(kPerSlot));
            const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            const auto memAfter = privateBytes();
            const int count = (int) host.getParameters().size();
            logMessage("16-slot layout cost check: " + juce::String(count) + " parameters, built in " + juce::String(ms, 1)
                       + " ms, about " + juce::String((memAfter - memBefore) / 1024) + " KiB of private memory");
            expectEquals(count, 16 * kPerSlot);
            // Measured 2026-10-19 on the development PC, Debug: 6,000 parameters in about 530 ms and 12 MB. The design's
            // budget (an editor opening in under a second) is for a Release build, which is several times faster; this Debug
            // run is held to the full second, so a layout that has grown past what the budget allows fails here first.
            expect(ms < 1000.0, "building 6,000 parameters took " + juce::String(ms, 1) + " ms");
            apvts.reset();
        }
    }
};

static MuTronicLayoutTests muTronicLayoutTests;
