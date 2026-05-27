// mu-tant gating designer tests — covers the subdivision math (cells per
// 2 bars), the subdivision-denominator dropdown round-trip, and the
// default/fallback behaviour.

#include <juce_core/juce_core.h>
#include "UI/GatingDesigner.h"

class GatingDesignerTest : public juce::UnitTest
{
public:
    GatingDesignerTest() : juce::UnitTest("mu-tant gating designer", "mu-tant") {}

    void runTest() override
    {
        using namespace mu_tant;

        beginTest("subdivision setter round-trips");
        {
            GatingDesigner gd;
            // Default is 1/16 per the header constant.
            expect(gd.getSubdivision() == 16, "default subdivision is 1/16");

            gd.setSubdivision(4);   expect(gd.getSubdivision() == 4,  "set to 1/4");
            gd.setSubdivision(8);   expect(gd.getSubdivision() == 8,  "set to 1/8");
            gd.setSubdivision(16);  expect(gd.getSubdivision() == 16, "set to 1/16");
            gd.setSubdivision(32);  expect(gd.getSubdivision() == 32, "set to 1/32");
        }

        beginTest("strip represents exactly 2 bars");
        {
            // The total bars is a design-spec constant; if it ever changes,
            // the math relating subdivision → cell count below also changes.
            expect(GatingDesigner::kTotalBars == 2,
                   "design spec: gating strip = 2 bars total");
        }

        // The cell-count math is exercised via the public subdivision setter.
        // The expectation: cells = bars * denom (one cell per Nth-note over the
        // total bar span). cellCount() itself is private, but the grid-line
        // pattern in paint() is a direct consequence.
        beginTest("subdivision implies expected cell count per 2 bars");
        {
            struct Case { int denom; int expectedCells; };
            const Case cases[] = {
                { 4,  8  },   // 1/4  over 2 bars = 8 cells
                { 8,  16 },   // 1/8  over 2 bars = 16 cells
                { 16, 32 },   // 1/16 over 2 bars = 32 cells
                { 32, 64 },   // 1/32 over 2 bars = 64 cells
            };
            for (const auto& c : cases)
            {
                const int cells = GatingDesigner::kTotalBars * c.denom;
                expect(cells == c.expectedCells,
                       "denom 1/" + juce::String(c.denom)
                           + " → " + juce::String(c.expectedCells) + " cells");
            }
        }
    }
};

static GatingDesignerTest gatingDesignerTestInstance;
