// SurfaceDescription and the control list: geometry counts, which controls exist, and the mock grid driver
// translating its own bottom-right numbering into the surface top-left coordinates.

#include "FakeDevicePorts.h"
#include "MockGridDriver.h"
#include "Surface/SurfaceMapping.h"
#include <set>

class SurfaceDescriptionTests : public juce::UnitTest
{
public:
    SurfaceDescriptionTests() : juce::UnitTest("Surface description", "MuControl") {}

    void runTest() override
    {
        using namespace mu_control;
        using namespace mu_control::test;

        beginTest("button counts and the control list match the geometry");
        {
            const auto lp = launchpadStyle();
            expectEquals(lp.buttonCount(ButtonKind::Top), 8);
            expectEquals(lp.buttonCount(ButtonKind::Side), 8);
            expectEquals(lp.buttonCount(ButtonKind::Special), 1);
            expectEquals((int) controlsOf(lp).size(), 64 + 8 + 8 + 1);

            const auto mock = mockGrid();
            expectEquals(mock.buttonCount(ButtonKind::Side), 0);
            expectEquals((int) controlsOf(mock).size(), 16 + 4);
        }

        beginTest("every listed control exists, none repeats, and off-surface ids do not exist");
        {
            for (const auto& d : { launchpadStyle(), mockGrid() })
            {
                std::set<SurfaceControlId> seen;
                for (const auto& id : controlsOf(d))
                {
                    expect(hasControl(d, id));
                    expect(seen.insert(id).second, "a control is listed once");
                }
                expect(! hasControl(d, { ControlKind::Pad, d.gridWidth, 0 }));
                expect(! hasControl(d, { ControlKind::Pad, 0, d.gridHeight }));
                expect(! hasControl(d, { ControlKind::Pad, -1, 0 }));
                expect(! hasControl(d, { ControlKind::Top, d.buttonCount(ButtonKind::Top), 0 }));
                expect(! hasControl(d, { ControlKind::Top, 0, 1 }));
            }
            expect(! hasControl(mockGrid(), { ControlKind::Side, 0, 0 }), "the mock has no side buttons");
            expect(! hasControl(mockGrid(), { ControlKind::Special, 0, 0 }));
        }

        beginTest("the mock grid driver reports in surface coordinates despite its bottom-right origin");
        {
            MockGridDriver drv;
            std::vector<SurfaceEvent> got;
            drv.onEvent = [&](const SurfaceEvent& e) { got.push_back(e); };

            drv.devicePad(0, 0, true);   // not open yet: nothing is reported
            expect(got.empty(), "a closed driver reports nothing");

            FakeDevicePorts ports;
            expect(drv.open(ports) && drv.isOpen());
            drv.devicePad(0, 0, true);   // device origin = bottom-right
            drv.devicePad(3, 3, false);  // device far corner = top-left
            drv.deviceTop(0, true);      // the rightmost top button
            expectEquals((int) got.size(), 3);
            expect(got[0].id == SurfaceControlId { ControlKind::Pad, 3, 3 } && got[0].type == SurfaceEventType::Press);
            expect(got[1].id == SurfaceControlId { ControlKind::Pad, 0, 0 } && got[1].type == SurfaceEventType::Release);
            expect(got[2].id == SurfaceControlId { ControlKind::Top, 3, 0 });

            drv.setLeds({ { { ControlKind::Pad, 0, 0 }, 0x112233, LedMode::Static } });
            drv.close();
            drv.setLeds({ { { ControlKind::Pad, 0, 0 }, 0x445566, LedMode::Static } });
            expectEquals((int) drv.sent.size(), 1, "a closed driver sends nothing");
        }
    }
};

static SurfaceDescriptionTests surfaceDescriptionTests;
