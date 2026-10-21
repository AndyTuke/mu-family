// The mu-Tronic scene picker in the surface model: hold the logo, the grid becomes eight whole-column scenes, a press on a
// lit column launches that scene. The hold is a state of the model, not a ControlAction. Run against the Launchpad-shaped
// description (it has a logo) and the mock grid (it has none, so it has no picker).

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

constexpr uint32_t kWhite = 0xFFFFFF;

// White x 0.75, the arrows' resting white (the "stored" scene), as the pad shows it.
uint32_t restWhite()
{
    const auto c = (uint32_t) std::lround(255.0f * 0.75f);
    return (c << 16) | (c << 8) | c;
}

SurfaceProfile pickerProfile(const SurfaceDescription& d)
{
    FakeLiveView offers;
    offers.sceneControls = true;
    return SurfaceProfile::forView(offers, d);
}

struct Rig
{
    explicit Rig(SurfaceDescription d, bool pickerOn = true)
        : desc(std::move(d)),
          model(sink, view, SurfaceMapping(desc), testLook(), [this] { return clockMs; },
                pickerOn ? pickerProfile(desc) : SurfaceProfile {})
    {
        view.mode = true;
        view.layers = 4;
        view.selected = 0;
        // Layer 1 stores clips 1 and 3 and plays clip 1; layer 2 stores clip 3 only; layer 3 stores clip 4, waiting.
        view.clipMask[0] = 0b0101;  view.playingClip[0] = 0;
        view.clipMask[1] = 0b0100;
        view.clipMask[2] = 0b1000;  view.queuedClip[2] = 3;
    }

    SurfaceDescription desc;
    FakeControlSink    sink;
    FakeLiveView       view;
    double             clockMs = 0.0;
    SurfaceModel       model;

    LedState led(const SurfaceControlId& id) const
    {
        for (const auto& l : model.render()) if (l.id == id) return l;
        return { id, 0xDEADBEEF, LedMode::Static };
    }
    bool handle(const SurfaceControlId& id, SurfaceEventType type = SurfaceEventType::Press)
    {
        return model.handle({ id, type, 0.0f });
    }
    static constexpr SurfaceControlId logo { ControlKind::Special, 0, 0 };
};
}

class SurfaceSceneTests : public juce::UnitTest
{
public:
    SurfaceSceneTests() : juce::UnitTest("Surface scene picker", "MuControl") {}

    void runTest() override
    {
        const auto look = testLook();

        beginTest("the profile offers the picker only to a product that says so, on a surface that has a logo");
        {
            FakeLiveView no, yes;
            yes.sceneControls = true;
            expect(! SurfaceProfile::forView(no, launchpadStyle()).sceneHold);
            expect(SurfaceProfile::forView(yes, launchpadStyle()).sceneHold == SurfaceControlId { ControlKind::Special, 0, 0 });
            expect(! SurfaceProfile::forView(yes, mockGrid()).sceneHold, "no logo, no picker");
        }

        beginTest("without the profile the logo does nothing and a pad still launches its clip");
        {
            Rig r(launchpadStyle(), false);
            expect(! r.handle(Rig::logo));
            expect(! r.model.isScenePickerOpen());
            expect(r.handle({ ControlKind::Pad, 2, 0 }));
            expect(r.sink.performed.size() == 1 && r.sink.performed[0].type == T::LaunchClip);
            expect(r.led(Rig::logo).rgb == 0, "the logo stays unlit");
        }

        beginTest("holding the logo opens the picker and releasing it closes it; neither performs an action");
        {
            Rig r(launchpadStyle());
            expect(! r.model.isScenePickerOpen());
            expect(! r.handle(Rig::logo), "the hold is not an action");
            expect(r.model.isScenePickerOpen());
            expect(! r.handle(Rig::logo, SurfaceEventType::Release));
            expect(! r.model.isScenePickerOpen());
            expect(r.sink.performed.empty(), "a logo tap with no pad does nothing");
        }

        beginTest("a pad press while the logo is held launches that column's scene, not a clip");
        {
            Rig r(launchpadStyle());
            r.handle(Rig::logo);
            expect(r.handle({ ControlKind::Pad, 2, 5 }), "any row of a column");
            expectEquals((int) r.sink.performed.size(), 1);
            if (! r.sink.performed.empty())
            {
                expect(r.sink.performed[0].type == T::LaunchScene);
                expectEquals(r.sink.performed[0].index, 2);
            }
            r.handle({ ControlKind::Pad, 2, 5 }, SurfaceEventType::Release);
            expectEquals((int) r.sink.performed.size(), 1, "pad releases do nothing");

            r.sink.accept = false;
            expect(! r.handle({ ControlKind::Pad, 6, 0 }), "the sink's answer is returned");
            expectEquals((int) r.sink.performed.size(), 2);   // a dark column still reaches the sink, so the screen can say it is empty
        }

        beginTest("the top row and the side column keep their meaning while the logo is held");
        {
            Rig r(launchpadStyle());
            r.handle(Rig::logo);
            expect(r.handle({ ControlKind::Top, 7, 0 }));
            expect(r.sink.performed.back().type == T::TransportToggle);
            expect(r.handle({ ControlKind::Side, 0, 2 }));
            expect(r.sink.performed.back().type == T::SelectLayer && r.sink.performed.back().layer == 2);
        }

        beginTest("a pad pressed again after the logo is released is a clip launch once more");
        {
            Rig r(launchpadStyle());
            r.handle(Rig::logo);
            r.handle(Rig::logo, SurfaceEventType::Release);
            r.handle({ ControlKind::Pad, 0, 0 });
            expect(r.sink.performed.back().type == T::LaunchClip);
        }

        beginTest("lights: columns are whole scenes in white, playing full, stored at rest white, queued pulsing, none dark");
        {
            Rig r(launchpadStyle());
            r.handle(Rig::logo);
            for (int y = 0; y < 8; ++y)
            {
                expectEquals((int) r.led({ ControlKind::Pad, 0, y }).rgb, (int) kWhite, "scene 1: layer 1 stores it and plays it");
                expectEquals((int) r.led({ ControlKind::Pad, 2, y }).rgb, (int) restWhite(), "scene 3: two layers store it, one plays it");
                expectEquals((int) r.led({ ControlKind::Pad, 1, y }).rgb, 0, "scene 2: nobody stores it");
                expectEquals((int) r.led({ ControlKind::Pad, 5, y }).rgb, 0, "scene 6: nobody stores it");
            }
            // Scene 4 waits on layer 3: a smooth pulse between dim and white at the queued rate (2 Hz, so a period is 500 ms).
            r.clockMs = 0.0;
            expectEquals((int) r.led({ ControlKind::Pad, 3, 0 }).rgb, (int) (std::lround(255.0f * look.dimScale) * 0x010101), "pulse starts at dim");
            r.clockMs = 250.0;
            expectEquals((int) r.led({ ControlKind::Pad, 3, 4 }).rgb, (int) kWhite, "and peaks at white");
            expect(r.model.isAnimating(), "a pulsing column needs the blink timer");
            expectEquals((int) r.led(Rig::logo).rgb, (int) kWhite, "the logo is lit white while it is held");
        }

        beginTest("a layer's own clip row shows nothing of itself in the picker: the pads that meant clips are replaced");
        {
            Rig r(launchpadStyle());
            // Layer 1 (row 0) stores clip 3 (column 3, index 2): normally a dim layer-coloured pad.
            const auto normal = r.led({ ControlKind::Pad, 2, 0 }).rgb;
            expect(normal != restWhite() && normal != 0);
            r.handle(Rig::logo);
            expectEquals((int) r.led({ ControlKind::Pad, 2, 0 }).rgb, (int) restWhite());
            r.handle(Rig::logo, SurfaceEventType::Release);
            expectEquals((int) r.led({ ControlKind::Pad, 2, 0 }).rgb, (int) normal, "releasing restores the grid at once");
            expectEquals((int) r.led(Rig::logo).rgb, 0, "and the logo goes dark again");
        }

        beginTest("an accepted press flashes its column white once; a press on a dark column shows no light");
        {
            Rig r(launchpadStyle());
            r.handle(Rig::logo);
            r.clockMs = 1000.0;
            expect(r.handle({ ControlKind::Pad, 2, 3 }));
            r.clockMs = 1050.0;
            expectEquals((int) r.led({ ControlKind::Pad, 2, 3 }).rgb, (int) kWhite, "the burst");
            r.clockMs = 1000.0 + look.stepFlashMs + 20.0;
            expectEquals((int) r.led({ ControlKind::Pad, 2, 3 }).rgb, (int) restWhite(), "then back to rest white");
            expect(r.model.isAnimating(), "the settle frame still has to go out");

            r.clockMs = 6000.0;
            r.handle({ ControlKind::Pad, 6, 3 });   // nobody stores scene 7
            r.clockMs = 6050.0;
            expectEquals((int) r.led({ ControlKind::Pad, 6, 3 }).rgb, 0);
        }

        beginTest("a clip of another layer type does not count toward a scene");
        {
            Rig r(launchpadStyle());
            r.view.foreignClips[1] = 0b0100;   // layer 2's clip 3 is another type's: it will not launch
            r.handle(Rig::logo);
            // Only layer 1 stores scene 3 now, and it plays it: the scene reads as playing.
            r.view.playingClip[0] = 2;
            r.view.notify();
            expectEquals((int) r.led({ ControlKind::Pad, 2, 0 }).rgb, (int) kWhite);
            // A scene stored by nobody who would take it is dark.
            r.view.foreignClips[0] = 0b0100;
            expectEquals((int) r.led({ ControlKind::Pad, 2, 0 }).rgb, 0);
        }

        beginTest("the picker does not survive the end of Performance mode, and a lost release is cleared");
        {
            Rig r(launchpadStyle());
            r.handle(Rig::logo);
            expect(r.model.isScenePickerOpen());
            r.view.mode = false;
            expect(r.led(Rig::logo).rgb == 0, "dark outside the mode");
            expect(! r.model.isScenePickerOpen());
            r.view.mode = true;
            expect(! r.handle(Rig::logo) && r.model.isScenePickerOpen());
            r.model.closeScenePicker();
            expect(! r.model.isScenePickerOpen(), "the manager closes it when the device goes");
            expect(! r.handle(Rig::logo, SurfaceEventType::Release) && ! r.model.isScenePickerOpen());
        }

        beginTest("the hold is ignored when the mode is off, and a held press repeated does not reopen anything");
        {
            Rig r(launchpadStyle());
            r.view.mode = false;
            r.handle(Rig::logo);
            expect(! r.model.isScenePickerOpen());
            r.view.mode = true;
            r.handle(Rig::logo);
            int changes = 0;
            r.model.onChanged = [&] { ++changes; };
            r.handle(Rig::logo);   // a second press of the held logo
            expectEquals(changes, 0);
        }

        beginTest("a surface without a logo has no picker even when the product offers one");
        {
            Rig r(mockGrid());
            expect(! r.model.isScenePickerOpen());
            expect(r.handle({ ControlKind::Pad, 1, 1 }));
            expect(r.sink.performed.back().type == T::LaunchClip);
        }
    }
};

static SurfaceSceneTests surfaceSceneTests;
