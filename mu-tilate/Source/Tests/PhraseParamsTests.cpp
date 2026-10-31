// The Phrase parameter table: every row the suffix table names exists under the slot's prefix (and only there),
// the rows' defaults are what the design says, and every modulation target names a real row with a proportional depth.

#include "Modulation/ModTargetChecks.h"   // mu-core: missingParams, depthNotProportional
#include "Modulation/MuTilateModDest.h"
#include "Persistence/PhraseParams.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

using namespace mu_tilate;

namespace
{
// The smallest processor that can own an APVTS.
struct Host : juce::AudioProcessor
{
    juce::AudioProcessorValueTreeState apvts;
    explicit Host(juce::AudioProcessorValueTreeState::ParameterLayout l)
        : juce::AudioProcessor(BusesProperties().withOutput("Out", juce::AudioChannelSet::stereo())), apvts(*this, nullptr, "T", std::move(l)) {}
    const juce::String getName() const override { return "T"; }
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

struct PhraseParamsTests : public juce::UnitTest
{
    PhraseParamsTests() : juce::UnitTest("Phrase parameters", "mu-tilate") {}

    void runTest() override
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        addLayerParams(layout, 0);
        addLayerParams(layout, 3);
        Host host(std::move(layout));
        auto& apvts = host.apvts;

        beginTest("every row exists under its slot's prefix, and the count matches the table");
        {
            for (int slot : { 0, 3 })
                for (int i = 0; i < kNumPhraseParams; ++i)
                    expect(apvts.getParameter(layerParamPrefix(slot) + ppi::suffix[i]) != nullptr, juce::String("missing ") + ppi::suffix[i]);
            expect(apvts.getParameter("p1_mode") == nullptr);
            int n = 0;
            for (auto* p : host.getParameters()) { juce::ignoreUnused(p); ++n; }
            expectEquals(n, 2 * kNumPhraseParams);
        }

        beginTest("defaults: Off, 120 BPM, a 10 ms hold at full strength, 1/16 grid, 4 bars");
        {
            auto get = [&](int row) { return apvts.getParameter(layerParamPrefix(0) + ppi::suffix[row]); };
            expectEquals(get(ppi::mode)->getValue(), get(ppi::mode)->convertTo0to1(0.0f));
            expectWithinAbsoluteError(get(ppi::ltempo)->convertFrom0to1(get(ppi::ltempo)->getDefaultValue()), 120.0f, 1.0e-3f);
            expectWithinAbsoluteError(get(ppi::hold)->convertFrom0to1(get(ppi::hold)->getDefaultValue()), 10.0f, 1.0e-3f);
            expectWithinAbsoluteError(get(ppi::strength)->convertFrom0to1(get(ppi::strength)->getDefaultValue()), 1.0f, 1.0e-6f);
            expectEquals(gridBeats(3), 0.25);
            expectEquals((int) std::lround(get(ppi::bars)->convertFrom0to1(get(ppi::bars)->getDefaultValue())), 4);
        }

        beginTest("every modulation target names a real row and its depth is a proportion of the range");
        {
            expectEquals(mu_mod::checks::missingParams(kModDestTable, apvts, "p0_").size(), 0);
            expectEquals(mu_mod::checks::depthNotProportional(kModDestTable).size(), 0);
            expect(isValidModDest("stretch.hold"));
            expect(! isValidModDest("stretch.curve"));
        }
    }
};

static PhraseParamsTests phraseParamsTests;
