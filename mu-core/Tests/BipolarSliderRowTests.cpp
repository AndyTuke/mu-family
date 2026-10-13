#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/BipolarSliderRow.h"

// BipolarSliderRow: the modulation depth / curve controls take the modulation knob colour.
struct BipolarSliderRowTests : juce::UnitTest
{
    BipolarSliderRowTests() : juce::UnitTest("BipolarSliderRow modulation colour", "mu-core") {}

    void runTest() override
    {
        beginTest("depth and curve sliders use the modulation knob colour");
        {
            BipolarSliderRow row;
            const auto want = MuLookAndFeel::colour(MuLookAndFeel::knobModulation);
            int sliders = 0;
            // Check every slider child (depth bar and curve knob) carries the fill colour.
            for (auto* c : row.getChildren())
                if (auto* sl = dynamic_cast<juce::Slider*>(c))
                {
                    ++sliders;
                    expect(sl->findColour(juce::Slider::rotarySliderFillColourId) == want, "slider fill is the modulation colour");
                }
            expectEquals(sliders, 2);
        }
    }
};

static BipolarSliderRowTests bipolarSliderRowTests;
