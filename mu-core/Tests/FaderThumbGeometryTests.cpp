#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/FaderThumbGeometry.h"

// Pure geometry of the fader cap, groove and accent fill (no drawing).
struct FaderThumbGeometryTests : juce::UnitTest
{
    FaderThumbGeometryTests() : juce::UnitTest("Fader thumb geometry", "mu-core") {}

    void runTest() override
    {
        namespace fd = mu_ui::fader;
        const juce::Rectangle<float> vArea(10.0f, 20.0f, 20.0f, 200.0f);
        const juce::Rectangle<float> hArea(10.0f, 20.0f, 200.0f, 18.0f);

        beginTest("cap size: nominal when it fits");
        {
            const auto c = fd::capSize(16.0f, 26.0f, 20.0f, 2.0f, 1.6f);
            expectWithinAbsoluteError(c.thickness, 16.0f, 0.001f);
            expectWithinAbsoluteError(c.length, 26.0f, 0.001f);
            expectWithinAbsoluteError(fd::thumbRadius(c), 13.0f, 0.001f);
        }

        beginTest("cap size: clamps to cross axis minus margin at 1.6 aspect");
        {
            const auto c = fd::capSize(16.0f, 26.0f, 12.0f, 2.0f, 1.6f);
            expectWithinAbsoluteError(c.thickness, 10.0f, 0.001f);
            expectWithinAbsoluteError(c.length, 16.0f, 0.001f);
            const auto z = fd::capSize(16.0f, 26.0f, 1.0f, 2.0f, 1.6f);
            expectWithinAbsoluteError(z.thickness, 0.0f, 0.001f);
        }

        beginTest("travel ends: vertical runs bottom to top, horizontal left to right");
        {
            const auto v = fd::travelEnds(vArea, false, 13.0f);
            expectWithinAbsoluteError(v.first, 207.0f, 0.001f);
            expectWithinAbsoluteError(v.second, 33.0f, 0.001f);
            const auto h = fd::travelEnds(hArea, true, 9.0f);
            expectWithinAbsoluteError(h.first, 19.0f, 0.001f);
            expectWithinAbsoluteError(h.second, 201.0f, 0.001f);
            expectWithinAbsoluteError(fd::positionForProportion(v, 0.5f), 120.0f, 0.001f);
        }

        beginTest("cap rect: centred across, on the slider position along the travel");
        {
            const auto cap = fd::capSize(16.0f, 26.0f, 20.0f, 2.0f, 1.6f);
            const auto v = fd::capRect(vArea, false, 100.0f, cap);
            expectWithinAbsoluteError(v.getWidth(), 16.0f, 0.001f);
            expectWithinAbsoluteError(v.getHeight(), 26.0f, 0.001f);
            expectWithinAbsoluteError(v.getCentreX(), vArea.getCentreX(), 0.001f);
            expectWithinAbsoluteError(v.getCentreY(), 100.0f, 0.001f);

            const auto h = fd::capRect(hArea, true, 80.0f, fd::capSize(16.0f, 26.0f, 18.0f, 2.0f, 1.6f));
            expectWithinAbsoluteError(h.getHeight(), 16.0f, 0.001f);
            expectWithinAbsoluteError(h.getWidth(), 26.0f, 0.001f);
            expectWithinAbsoluteError(h.getCentreX(), 80.0f, 0.001f);
            expectWithinAbsoluteError(h.getCentreY(), hArea.getCentreY(), 0.001f);
        }

        beginTest("cap stays inside the area at both ends of the travel");
        {
            const auto cap = fd::capSize(16.0f, 26.0f, 20.0f, 2.0f, 1.6f);
            const auto ends = fd::travelEnds(vArea, false, fd::thumbRadius(cap));
            expect(vArea.contains(fd::capRect(vArea, false, ends.first, cap)));
            expect(vArea.contains(fd::capRect(vArea, false, ends.second, cap)));
        }

        beginTest("groove: centred, spans the travel, either end order");
        {
            const auto ends = fd::travelEnds(vArea, false, 13.0f);
            const auto g = fd::grooveRect(vArea, false, 4.0f, ends);
            expectWithinAbsoluteError(g.getWidth(), 4.0f, 0.001f);
            expectWithinAbsoluteError(g.getCentreX(), vArea.getCentreX(), 0.001f);
            expectWithinAbsoluteError(g.getY(), 33.0f, 0.001f);
            expectWithinAbsoluteError(g.getBottom(), 207.0f, 0.001f);
            const auto h = fd::grooveRect(hArea, true, 4.0f, fd::travelEnds(hArea, true, 9.0f));
            expectWithinAbsoluteError(h.getHeight(), 4.0f, 0.001f);
            expectWithinAbsoluteError(h.getX(), 19.0f, 0.001f);
            expectWithinAbsoluteError(h.getRight(), 201.0f, 0.001f);
        }

        beginTest("fill span: unipolar from the minimum end, bipolar from zero, sorted");
        {
            // Vertical: minimum end is the larger y; the span is returned low to high.
            const auto uni = fd::fillSpan(207.0f, 120.0f, 80.0f, false);
            expectWithinAbsoluteError(uni.first, 80.0f, 0.001f);
            expectWithinAbsoluteError(uni.second, 207.0f, 0.001f);
            const auto up = fd::fillSpan(207.0f, 120.0f, 60.0f, true);
            expectWithinAbsoluteError(up.first, 60.0f, 0.001f);
            expectWithinAbsoluteError(up.second, 120.0f, 0.001f);
            const auto down = fd::fillSpan(207.0f, 120.0f, 180.0f, true);
            expectWithinAbsoluteError(down.first, 120.0f, 0.001f);
            expectWithinAbsoluteError(down.second, 180.0f, 0.001f);
            const auto none = fd::fillSpan(207.0f, 120.0f, 120.0f, true);
            expectWithinAbsoluteError(none.second - none.first, 0.0f, 0.001f);
        }

        beginTest("bipolar: only a range that straddles zero");
        {
            expect(fd::isBipolar(-100.0, 100.0));
            expect(! fd::isBipolar(0.0, 1.0));
            expect(! fd::isBipolar(-1.0, 0.0));
        }
    }
};

static FaderThumbGeometryTests faderThumbGeometryTests;
