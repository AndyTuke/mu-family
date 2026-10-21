// Layer pages in the surface model: a product that pages its layers gets PagePrev / PageNext on top-row 5 and 6 in place of the
// clip arrows, and the grid rows and side column mean the layers of the shown page. The page is the page of the selected layer.

#include "FakeControlSink.h"
#include "FakeLiveView.h"
#include "MockGridDriver.h"
#include "Surface/SurfaceModel.h"
#include <cmath>

namespace
{
using namespace mu_control;
using namespace mu_control::test;
using T = mu_core::ControlActionType;

constexpr SurfaceControlId kTop5 { ControlKind::Top, 4, 0 };
constexpr SurfaceControlId kTop6 { ControlKind::Top, 5, 0 };

SurfaceProfile profileFor(bool pages, const SurfaceDescription& d)
{
    FakeLiveView v;
    v.layerPages = pages;
    return SurfaceProfile::forView(v, d);
}

struct Rig
{
    explicit Rig(SurfaceDescription d, bool pagesOn = true)
        : desc(std::move(d)),
          model(sink, view, SurfaceMapping(desc), testLook(), [] { return 0.0; }, profileFor(pagesOn, desc))
    {
        view.mode = true;
        view.layers = 16;
        view.selected = 2;
    }

    SurfaceDescription desc;
    FakeControlSink    sink;
    FakeLiveView       view;
    SurfaceModel       model;

    LedState led(const SurfaceControlId& id) const
    {
        for (const auto& l : model.render()) if (l.id == id) return l;
        return { id, 0xDEADBEEF, LedMode::Static };
    }
    bool press(const SurfaceControlId& id) { return model.handle({ id, SurfaceEventType::Press, 0.0f }); }
};
}

class SurfacePageTests : public juce::UnitTest
{
public:
    SurfacePageTests() : juce::UnitTest("Surface layer pages", "MuControl") {}

    void runTest() override
    {
        const auto look = testLook();
        const auto grey = [](float s) { const auto c = (uint32_t) std::lround(255.0f * s); return (c << 16) | (c << 8) | c; };
        const uint32_t dim  = grey(look.dimScale);
        const uint32_t rest = grey(0.75f);   // the arrows' resting white

        beginTest("the profile pages only for a product that says so");
        {
            FakeLiveView no, yes;
            yes.layerPages = true;
            expect(! SurfaceProfile::forView(no, launchpadStyle()).layerPages);
            expect(SurfaceProfile::forView(yes, launchpadStyle()).layerPages);
        }

        beginTest("the view's page is the selected layer's page, and the page count follows the layer count");
        {
            FakeLiveView v;
            v.layers = 16; v.selected = 7;  expectEquals(v.getShownPage(), 0);
            v.selected = 8;                 expectEquals(v.getShownPage(), 1);
            v.selected = 15;                expectEquals(v.getShownPage(), 1);
            expectEquals(v.getPageCount(), 2);
            v.layers = 8;                   expectEquals(v.getPageCount(), 1);
            v.layers = 9;                   expectEquals(v.getPageCount(), 2);
        }

        beginTest("without pages 5 and 6 stay the clip arrows");
        {
            Rig r(launchpadStyle(), false);
            r.press(kTop5); r.press(kTop6);
            expect(r.sink.performed.size() == 2 && r.sink.performed[0].type == T::ClipPrev && r.sink.performed[1].type == T::ClipNext);
        }

        beginTest("with pages 5 and 6 send PagePrev and PageNext, and the other top-row buttons are unchanged");
        {
            Rig r(launchpadStyle());
            const T expected[8] = { T::AppPrev, T::AppNext, T::PerformancePrev, T::PerformanceNext,
                                    T::PagePrev, T::PageNext, T::MuteSelectedLayer, T::TransportToggle };
            for (int i = 0; i < 8; ++i)
            {
                r.press({ ControlKind::Top, i, 0 });
                expect(r.sink.performed.back().type == expected[i], "button " + juce::String(i + 1));
            }
        }

        beginTest("page buttons light white while there is a page that way and dim at the ends (no wrap)");
        {
            Rig r(launchpadStyle());
            r.view.selected = 2;   // page 1 of 2
            expectEquals(r.led(kTop5).rgb, dim, "first page: previous is dim");
            expectEquals(r.led(kTop6).rgb, rest, "first page: next is bright");
            r.view.selected = 10;  // page 2
            expectEquals(r.led(kTop5).rgb, rest);
            expectEquals(r.led(kTop6).rgb, dim, "last page: next is dim");
            r.view.layers = 8;     // one page
            r.view.selected = 2;
            expectEquals(r.led(kTop5).rgb, dim);
            expectEquals(r.led(kTop6).rgb, dim);
        }

        beginTest("the rows are the layers of the shown page: pad and side presses carry the page offset");
        {
            Rig r(launchpadStyle());
            r.view.selected = 2;
            r.press({ ControlKind::Pad, 3, 0 });
            expect(r.sink.performed.back().type == T::LaunchClip && r.sink.performed.back().layer == 0 && r.sink.performed.back().index == 3);
            r.view.selected = 9;   // page 2
            r.press({ ControlKind::Pad, 3, 0 });
            expect(r.sink.performed.back().layer == 8 && r.sink.performed.back().index == 3, "top row = slot 9");
            r.press({ ControlKind::Side, 0, 0 });
            expect(r.sink.performed.back().type == T::SelectLayer && r.sink.performed.back().layer == 8);
        }

        beginTest("a row past the last layer is a dark pad that does nothing");
        {
            Rig r(launchpadStyle());
            r.view.layers = 10;
            r.view.selected = 9;   // page 2 holds slots 9 and 10
            expect(r.press({ ControlKind::Side, 0, 0 }));
            r.sink.performed.clear();
            expect(! r.press({ ControlKind::Side, 0, 2 }), "slot 11 does not exist");
            expect(r.sink.performed.empty());
        }

        beginTest("the grid lights the clips of the shown page's layers");
        {
            Rig r(launchpadStyle());
            r.view.clipMask[0] = 0b1;  r.view.playingClip[0] = 0;   // slot 1
            r.view.clipMask[8] = 0b10; r.view.playingClip[8] = 1;   // slot 9
            r.view.selected = 2;
            expect(r.led({ ControlKind::Pad, 0, 0 }).rgb != 0, "page 1: slot 1 clip 1 lit");
            expectEquals((int) r.led({ ControlKind::Pad, 1, 0 }).rgb, 0, "page 1: slot 9's clip is not shown");
            r.view.selected = 9;
            expectEquals((int) r.led({ ControlKind::Pad, 0, 0 }).rgb, 0, "page 2: slot 1's clip is gone");
            expect(r.led({ ControlKind::Pad, 1, 0 }).rgb != 0, "page 2: slot 9 clip 2 lit");
        }

        beginTest("a refused page press changes no light");
        {
            Rig r(launchpadStyle());
            r.sink.accept = false;
            const auto before = r.model.render();
            expect(! r.press(kTop5));
            expect(before == r.model.render());
        }
    }
};

static SurfacePageTests surfacePageTests;
