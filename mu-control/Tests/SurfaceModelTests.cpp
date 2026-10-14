// SurfaceModel: presses to actions and the light frame, run against a Launchpad-shaped description, the mock
// grid (palette colour, no blink, no side buttons) and a native-blink surface. The same model code and the same
// default mapping must serve all of them; a case that needs a feature skips the surfaces without it.

#include "FakeControlSink.h"
#include "FakeLiveView.h"
#include "MockGridDriver.h"
#include "Surface/SurfaceModel.h"
#include <cmath>

namespace
{
int chan(uint32_t rgb, int shift) { return (int) ((rgb >> shift) & 0xFFu); }

using namespace mu_control;
using namespace mu_control::test;
using T = mu_core::ControlActionType;

// Brightness scale as the pad shows it (rounded per channel).
uint32_t scaled(uint32_t rgb, float s)
{
    const auto ch = [&](int shift) { return (uint32_t) std::lround((float) ((rgb >> shift) & 0xFFu) * s); };
    return (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

struct Rig
{
    explicit Rig(SurfaceDescription d, double* clockMs = nullptr)
        : desc(std::move(d)),
          model(sink, view, SurfaceMapping(desc), testLook(),
                [clockMs] { return clockMs != nullptr ? *clockMs : 0.0; })
    {
        for (int i = 0; i < 4; ++i) view.useItem(i);
        view.playable[2] = false;   // item 3 is in the list but its file is gone
        view.current = 0;
        view.layers = 4;
        view.selected = 1;
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
    static SurfaceEvent press(const SurfaceControlId& id) { return { id, SurfaceEventType::Press, 0.0f }; }
};

std::vector<SurfaceDescription> allSurfaces() { return { launchpadStyle(), mockGrid(), nativeBlink() }; }
}

class SurfaceModelTests : public juce::UnitTest
{
public:
    SurfaceModelTests() : juce::UnitTest("Surface model", "MuControl") {}

    void runTest() override
    {
        const auto look = testLook();

        beginTest("a top press performs its table action on every surface (a smaller surface has the first buttons)");
        for (const auto& d : allSurfaces())
        {
            const T table[8] = { T::AppPrev, T::AppNext, T::PerformancePrev, T::PerformanceNext,
                                 T::ClipPrev, T::ClipNext, T::MuteSelectedLayer, T::TransportToggle };
            Rig r(d);
            const int n = d.buttonCount(ButtonKind::Top);
            for (int i = 0; i < n; ++i)
            {
                expect(r.model.handle(Rig::press({ ControlKind::Top, i, 0 })), d.name);
                expect(r.sink.performed.back().type == table[i], d.name + ": button " + juce::String(i + 1));
            }
            expectEquals((int) r.sink.performed.size(), n);
        }

        beginTest("a side press performs SelectLayer, top to bottom, only where the surface has side buttons");
        for (const auto& d : allSurfaces())
        {
            Rig r(d);
            if (d.buttonCount(ButtonKind::Side) == 0)
            {
                expect(! r.model.handle(Rig::press({ ControlKind::Side, 0, 0 })), d.name + ": no such control");
                expect(r.sink.performed.empty());
                continue;
            }
            expect(r.model.handle(Rig::press({ ControlKind::Side, 0, 0 })));   // top button
            expect(r.model.handle(Rig::press({ ControlKind::Side, 0, 3 })));
            expectEquals(r.sink.performed[0].layer, 0);
            expectEquals(r.sink.performed[1].layer, 3);
            expect(r.sink.performed[1].type == T::SelectLayer);
        }

        beginTest("a grid press performs LaunchClip(layer, clip) with layer 1 on the top row");
        for (const auto& d : allSurfaces())
        {
            Rig r(d);
            expect(r.model.handle(Rig::press({ ControlKind::Pad, 1, 0 })), d.name);
            expect(r.sink.performed[0].type == T::LaunchClip && r.sink.performed[0].layer == 0 && r.sink.performed[0].index == 1, d.name);
            expect(r.model.handle(Rig::press({ ControlKind::Pad, 2, 3 })), d.name);
            expect(r.sink.performed[1].layer == 3 && r.sink.performed[1].index == 2, d.name);
        }

        beginTest("a release or a value does nothing, and a press on a layer that does not exist is not performed");
        for (const auto& d : allSurfaces())
        {
            Rig r(d);
            expect(! r.model.handle({ { ControlKind::Top, 1, 0 }, SurfaceEventType::Release, 0.0f }), d.name);
            expect(! r.model.handle({ { ControlKind::Top, 1, 0 }, SurfaceEventType::Value, 0.5f }), d.name);
            r.view.layers = 2;
            expect(! r.model.handle(Rig::press({ ControlKind::Pad, 0, d.gridHeight - 1 })), d.name + ": a row past the layer count");
            expect(r.sink.performed.empty(), d.name);
            if (d.buttonCount(ButtonKind::Side) > 0)
            {
                expect(! r.model.handle(Rig::press({ ControlKind::Side, 0, 7 })));   // layer 8 of 2
                expect(r.sink.performed.empty());
            }
        }

        beginTest("the handle result is the sink answer");
        for (const auto& d : allSurfaces())
        {
            Rig r(d);
            r.sink.accept = false;
            expect(! r.model.handle(Rig::press({ ControlKind::Top, 0, 0 })), d.name);
            expectEquals((int) r.sink.performed.size(), 1, "the action was still offered to the sink");
        }

        beginTest("the frame has one light per control, all dark outside Performance mode");
        for (const auto& d : allSurfaces())
        {
            Rig r(d);
            expectEquals((int) r.model.render().size(), (int) controlsOf(d).size(), d.name);
            r.view.mode = false;
            for (const auto& l : r.model.render()) expectEquals((int) l.rgb, 0, d.name);
        }

        beginTest("app arrows: dark and inert with no other app, the app accent when one is available");
        for (const auto& d : allSurfaces())
        {
            Rig r(d);
            expectEquals((int) r.led({ ControlKind::Top, 0, 0 }).rgb, 0, d.name + ": AppPrev dark");
            expectEquals((int) r.led({ ControlKind::Top, 1, 0 }).rgb, 0, d.name + ": AppNext dark");
            r.sink.accept = false;   // the sink refuses AppPrev / AppNext while no hub exists
            const auto before = r.model.render();
            expect(! r.model.handle(Rig::press({ ControlKind::Top, 0, 0 })), d.name);
            expect(before == r.model.render(), d.name + ": a refused press changes no light");

            r.view.otherApp = true;
            expectEquals(r.led({ ControlKind::Top, 0, 0 }).rgb, look.accentRgb, d.name + ": lit in the app accent");
            expectEquals(r.led({ ControlKind::Top, 1, 0 }).rgb, look.accentRgb, d.name);
        }

        beginTest("preset arrows: neutral white below full, off with fewer than two playable items");
        for (const auto& d : allSurfaces())
        {
            Rig r(d);   // items 1, 2 and 4 are playable
            const auto prev = r.led({ ControlKind::Top, 2, 0 }), next = r.led({ ControlKind::Top, 3, 0 });
            expect(prev.rgb != 0 && prev.rgb == next.rgb, d.name + ": both lit alike");
            expect(chan(prev.rgb, 16) == chan(prev.rgb, 8) && chan(prev.rgb, 8) == chan(prev.rgb, 0), d.name + ": neutral white");
            expect(prev.rgb < look.whiteRgb, d.name + ": at rest below full white, so the press flash shows");

            r.view.playable = { true, false, false, false, false, false, false, false };   // one playable item
            expectEquals((int) r.led({ ControlKind::Top, 2, 0 }).rgb, 0, d.name + ": one item: off");
            expectEquals((int) r.led({ ControlKind::Top, 3, 0 }).rgb, 0, d.name + ": one item: off");
            r.view.playable = { true, true, false, false, false, false, false, false };
            expect(r.led({ ControlKind::Top, 3, 0 }).rgb != 0, d.name + ": two items: lit");
            r.view.mode = false;
            expectEquals((int) r.led({ ControlKind::Top, 3, 0 }).rgb, 0, d.name + ": dark outside the mode");
        }

        beginTest("an accepted press on an arrow flashes it white for stepFlashMs, a refused one changes nothing");
        {
            double now = 1000.0;
            Rig lp(launchpadStyle(), &now);
            const SurfaceControlId next { ControlKind::Top, 3, 0 };
            const auto rest = lp.led(next).rgb;
            int renders = 0;
            lp.model.onChanged = [&] { ++renders; };
            expect(lp.model.handle(Rig::press(next)));
            expect(renders >= 1, "the accepted press asks for a render, so the flash is drawn");
            expectEquals(lp.led(next).rgb, look.whiteRgb, "white at once");
            expect(lp.model.isAnimating());
            now = 1149.0;
            expectEquals(lp.led(next).rgb, look.whiteRgb, "still flashing");
            now = 1160.0;
            expectEquals(lp.led(next).rgb, rest, "back to its steady light");
            expect(lp.model.isAnimating(), "the timer runs a moment longer so the steady frame is sent");
            now = 1400.0;
            expect(! lp.model.isAnimating());

            // The app arrows flash too (white over the accent) once another app exists and the sink takes the press.
            lp.view.otherApp = true;
            expect(lp.model.handle(Rig::press({ ControlKind::Top, 0, 0 })));
            expectEquals(lp.led({ ControlKind::Top, 0, 0 }).rgb, look.whiteRgb);

            Rig refused(launchpadStyle(), &now);
            refused.sink.accept = false;
            const auto before = refused.model.render();
            expect(! refused.model.handle(Rig::press(next)));
            expect(before == refused.model.render(), "a refused press: no flash");
            expect(! refused.model.isAnimating());
        }

        beginTest("a preset arrow pulses white from its press until the switch commits or is cancelled");
        {
            double now = 0.0;
            Rig lp(launchpadStyle(), &now);
            const SurfaceControlId next { ControlKind::Top, 3, 0 }, prev { ControlKind::Top, 2, 0 };
            const auto rest = lp.led(next).rgb;
            lp.view.queued = 1;                       // the switch the press queues
            expect(lp.model.handle(Rig::press(next)));
            now = 500.0;                              // past the flash, a whole pulse period on: dim
            expectEquals(lp.led(next).rgb, scaled(look.whiteRgb, look.dimScale), "pulse at its dim end");
            now = 750.0;
            expectEquals(lp.led(next).rgb, look.whiteRgb, "and its bright end");
            expect(lp.model.isAnimating());
            expectEquals(lp.led(prev).rgb, rest, "only the pressed arrow pulses");
            lp.view.queued = -1;                      // committed or cancelled
            expectEquals(lp.led(next).rgb, rest, "steady again");
            lp.view.queued = 2;                       // a later queue (from the screen) is not this arrow's
            expectEquals(lp.led(next).rgb, rest);

            Rig nb(nativeBlink());                    // no RGB, native pulse: the device blinks it
            nb.view.queued = 1;
            nb.model.handle(Rig::press({ ControlKind::Top, 3, 0 }));
            Rig mock(mockGrid());
            mock.view.queued = 1;
            mock.model.handle(Rig::press({ ControlKind::Top, 3, 0 }));
            expect(! mock.model.isAnimating(), "a palette surface has no timer");
        }

        beginTest("clip arrows: dim white when the selected layer stores two or more clips, else off");
        for (const auto& d : allSurfaces())
        {
            if (d.buttonCount(ButtonKind::Top) < 6) continue;
            Rig r(d);
            r.view.selected = 1;
            r.view.clipMask[1] = 0b0101;
            expectEquals(r.led({ ControlKind::Top, 4, 0 }).rgb, scaled(look.whiteRgb, look.dimScale), d.name);
            expectEquals(r.led({ ControlKind::Top, 5, 0 }).rgb, scaled(look.whiteRgb, look.dimScale), d.name);
            r.view.clipMask[1] = 0b0100;
            expectEquals((int) r.led({ ControlKind::Top, 4, 0 }).rgb, 0, d.name + ": one clip: off");
            r.view.clipMask[1] = 0;
            r.view.clipMask[0] = 0xFF;   // another layer's clips do not count
            expectEquals((int) r.led({ ControlKind::Top, 5, 0 }).rgb, 0, d.name + ": only the selected layer counts");
            r.view.selected = 0;
            expect(r.led({ ControlKind::Top, 5, 0 }).rgb != 0, d.name + ": the lights follow the selection");
            r.view.selected = 6;   // no such layer (4 live)
            expectEquals((int) r.led({ ControlKind::Top, 5, 0 }).rgb, 0, d.name + ": no layer: off");
            r.view.selected = 0;
            r.view.mode = false;
            expectEquals((int) r.led({ ControlKind::Top, 5, 0 }).rgb, 0, d.name + ": dark outside the mode");
        }

        beginTest("mute button: the selected layer's colour, dim when unmuted, flashing when muted");
        {
            double now = 0.0;
            Rig lp(launchpadStyle(), &now);
            const SurfaceControlId mute { ControlKind::Top, 6, 0 };
            lp.view.selected = 1;
            expectEquals(lp.led(mute).rgb, scaled(look.layerRgb[1], look.dimScale), "unmuted: dim");
            expect(! lp.model.isAnimating());
            lp.view.muted[1] = true;
            expectEquals(lp.led(mute).rgb, look.layerRgb[1], "muted: bright half");
            expect(lp.model.isAnimating());
            now = 500.0;
            expectEquals(lp.led(mute).rgb, scaled(look.layerRgb[1], look.dimScale), "muted: dim half, never dark");
            lp.view.selected = 2;
            expectEquals(lp.led(mute).rgb, scaled(look.layerRgb[2], look.dimScale), "follows the selection");
            lp.view.selected = 5;
            expectEquals((int) lp.led(mute).rgb, 0, "no such layer: off");
            lp.view.selected = 1;
            expect(lp.model.handle(Rig::press(mute)));
            expect(lp.sink.performed.back().type == T::MuteSelectedLayer);
        }

        beginTest("transport button: green when stopped, red when playing, static dim white when a clock owns it");
        for (const auto& d : allSurfaces())
        {
            if (d.buttonCount(ButtonKind::Top) < 8) continue;
            Rig r(d);
            const SurfaceControlId play { ControlKind::Top, 7, 0 };
            expectEquals(r.led(play).rgb, look.goRgb, d.name + ": stopped");
            expect(r.led(play).mode == LedMode::Static, d.name);
            r.view.playing = true;
            r.view.notify();
            expectEquals(r.led(play).rgb, look.faultRgb, d.name + ": playing");
            expect(r.model.handle(Rig::press(play)), d.name);
            expect(r.sink.performed.back().type == T::TransportToggle, d.name);
            expect(! r.model.isAnimating(), d.name + ": a transport press needs no flash");

            r.view.outside = true;
            r.sink.performed.clear();
            expectEquals(r.led(play).rgb, scaled(look.whiteRgb, look.dimScale), d.name + ": owned elsewhere: dim white");
            expect(! r.model.isAnimating(), d.name + ": static, not blinking");
            const auto before = r.model.render();
            expect(! r.model.handle(Rig::press(play)), d.name + ": the press is ignored");
            expect(r.sink.performed.empty(), d.name + ": and never reaches the sink");
            expect(before == r.model.render(), d.name);
            r.view.playing = false;   // owned: the clock decides, the internal flag is irrelevant
            expectEquals(r.led(play).rgb, scaled(look.whiteRgb, look.dimScale), d.name);

            // A press the sink refuses changes no light either.
            r.view.outside = false;
            r.sink.accept = false;
            const auto stopped = r.model.render();
            expect(! r.model.handle(Rig::press(play)), d.name);
            expect(stopped == r.model.render(), d.name);
        }

        beginTest("dim is exactly dimScale times bright, for the layer colours");
        for (const auto& d : allSurfaces())
        {
            Rig r(d);
            if (d.buttonCount(ButtonKind::Side) == 0) continue;
            const auto selected = r.led({ ControlKind::Side, 0, 1 }).rgb;   // layer 2 is selected
            const auto other    = r.led({ ControlKind::Side, 0, 0 }).rgb;   // layer 1
            expectEquals(selected, look.layerRgb[1]);
            expectEquals(other, scaled(look.layerRgb[0], look.dimScale));
        }

        beginTest("side column: the selected layer bright in its colour, others dim, absent layers off");
        for (const auto& d : allSurfaces())
        {
            if (d.buttonCount(ButtonKind::Side) == 0) continue;
            Rig r(d);
            r.view.colourIndex = { 5, 4, 3, 2, 1, 0, 7, 6 };
            r.view.layers = 3;
            r.view.selected = 2;
            expectEquals(r.led({ ControlKind::Side, 0, 2 }).rgb, look.layerRgb[3], "layer 3 selected: its colour index 3");
            expectEquals(r.led({ ControlKind::Side, 0, 1 }).rgb, scaled(look.layerRgb[4], look.dimScale), "layer 2 dim");
            expectEquals((int) r.led({ ControlKind::Side, 0, 3 }).rgb, 0, "layer 4 does not exist");
            expectEquals((int) r.led({ ControlKind::Side, 0, 7 }).rgb, 0, "layer 8 does not exist");
        }

        beginTest("the grid is dark while no clip is stored");
        for (const auto& d : allSurfaces())
        {
            Rig r(d);
            for (const auto& l : r.model.render())
                if (l.id.kind == ControlKind::Pad) expectEquals((int) l.rgb, 0, d.name);
        }

        beginTest("grid: playing bright, stored dim, empty off, in the layer colour; layer 1 is the top row");
        for (const auto& d : allSurfaces())
        {
            Rig r(d);
            r.view.colourIndex = { 5, 4, 3, 2, 1, 0, 7, 6 };
            r.view.clipMask[0] = 0b0111;          // layer 1: clips 1 to 3 stored
            r.view.playingClip[0] = 1;
            r.view.clipMask[2] = 0b0001;          // layer 3: clip 1 stored, none playing
            const int bottom = 0;   // layer 1's row
            expectEquals(r.led({ ControlKind::Pad, 1, bottom }).rgb, look.layerRgb[5], d.name + ": playing");
            expectEquals(r.led({ ControlKind::Pad, 0, bottom }).rgb, scaled(look.layerRgb[5], look.dimScale), d.name + ": stored");
            expectEquals(r.led({ ControlKind::Pad, 2, bottom }).rgb, scaled(look.layerRgb[5], look.dimScale), d.name + ": stored");
            expectEquals((int) r.led({ ControlKind::Pad, 3, bottom }).rgb, 0, d.name + ": empty");
            expectEquals(r.led({ ControlKind::Pad, 0, bottom + 2 }).rgb, scaled(look.layerRgb[3], look.dimScale), d.name + ": layer 3, nothing playing");
            expectEquals((int) r.led({ ControlKind::Pad, 0, bottom + 1 }).rgb, 0, d.name + ": layer 2 has no clips");
        }

        beginTest("grid: a row with no layer is off even if clip state is stale");
        for (const auto& d : allSurfaces())
        {
            Rig r(d);
            r.view.layers = 2;
            r.view.clipMask[2] = 0xFF; r.view.playingClip[2] = 0;
            const int row = 2;
            for (int x = 0; x < d.gridWidth; ++x) expectEquals((int) r.led({ ControlKind::Pad, x, row }).rgb, 0, d.name);
        }

        beginTest("grid: a queued clip pulses between dim and bright (RGB) and the playing one stays bright");
        {
            double now = 0.0;
            Rig lp(launchpadStyle(), &now);
            lp.view.clipMask[0] = 0b0011; lp.view.playingClip[0] = 0; lp.view.queuedClip[0] = 1;
            const SurfaceControlId queuedPad { ControlKind::Pad, 1, 0 }, playingPad { ControlKind::Pad, 0, 0 };
            lp.led(playingPad);                           // first sighting of the playing clip
            expectEquals(lp.led(queuedPad).rgb, scaled(look.layerRgb[0], look.dimScale), "pulse starts dim");
            expect(lp.model.isAnimating(), "a pulsing pad needs the blink timer");
            now = 250.0;
            expectEquals(lp.led(queuedPad).rgb, look.layerRgb[0], "and peaks bright");
            expectEquals(lp.led(playingPad).rgb, look.layerRgb[0], "the playing pad is steady bright");

            Rig nb(nativeBlink());
            nb.view.clipMask[0] = 0b11; nb.view.playingClip[0] = 0; nb.view.queuedClip[0] = 1;
            expect(nb.led({ ControlKind::Pad, 1, 0 }).mode == LedMode::Pulse, "no RGB but a native pulse");
            Rig mock(mockGrid());
            mock.view.clipMask[0] = 0b11; mock.view.playingClip[0] = 0; mock.view.queuedClip[0] = 1;
            expect(mock.led({ ControlKind::Pad, 1, 0 }).mode == LedMode::Static, "no pulse: steady");
            expect(! mock.model.isAnimating());
        }

        beginTest("grid: the playing clip of a muted layer flashes square between bright and dim, never dark");
        {
            double now = 0.0;
            Rig lp(launchpadStyle(), &now);
            lp.view.clipMask[0] = 0b0011; lp.view.playingClip[0] = 0; lp.view.muted[0] = true;
            const SurfaceControlId playingPad { ControlKind::Pad, 0, 0 };
            expectEquals(lp.led(playingPad).rgb, look.layerRgb[0], "first half of the period: bright");
            expect(lp.model.isAnimating());
            now = 499.0;
            expectEquals(lp.led(playingPad).rgb, look.layerRgb[0], "still bright just before half");
            now = 500.0;
            expectEquals(lp.led(playingPad).rgb, scaled(look.layerRgb[0], look.dimScale), "second half: dim, not off");
            now = 1000.0;
            expectEquals(lp.led(playingPad).rgb, look.layerRgb[0], "and back");
            lp.view.muted[0] = false;
            now = 1600.0;
            expectEquals(lp.led(playingPad).rgb, look.layerRgb[0], "unmuted: steady bright");

            Rig nb(nativeBlink());
            nb.view.clipMask[0] = 0b1; nb.view.playingClip[0] = 0; nb.view.muted[0] = true;
            expect(nb.led({ ControlKind::Pad, 0, 0 }).mode == LedMode::Flash, "no RGB but a native flash");
            Rig mock(mockGrid());
            mock.view.clipMask[0] = 0b1; mock.view.playingClip[0] = 0; mock.view.muted[0] = true;
            const auto m = mock.led({ ControlKind::Pad, 0, 0 });
            expect(m.mode == LedMode::Static && m.rgb != 0, "no flash at all: steady and lit");
        }

        beginTest("grid: a clip that just started plays one brief burst, then settles to steady bright");
        {
            double now = 1000.0;
            Rig lp(launchpadStyle(), &now);
            lp.view.clipMask[0] = 0b0011;
            const SurfaceControlId pad { ControlKind::Pad, 1, 0 };
            lp.led(pad);                                  // first sighting: nothing playing yet
            lp.view.playingClip[0] = 1;
            expect(lp.led(pad).rgb != look.layerRgb[0], "the change starts a burst");
            expect(lp.model.isAnimating());
            now = 1149.0;
            expect(lp.led(pad).rgb != look.layerRgb[0], "still in the burst");
            now = 1160.0;
            expectEquals(lp.led(pad).rgb, look.layerRgb[0], "steady after stepFlashMs");
            expect(lp.model.isAnimating(), "the timer runs a moment longer so the steady frame is sent");
            now = 1400.0;
            expect(! lp.model.isAnimating(), "then stops");

            Rig mock(mockGrid());
            mock.view.clipMask[0] = 0b1;
            mock.led({ ControlKind::Pad, 0, 0 });
            mock.view.playingClip[0] = 0;
            expectEquals(mock.led({ ControlKind::Pad, 0, 0 }).rgb, look.layerRgb[0], "a palette pad has no burst");
            expect(! mock.model.isAnimating());
        }

        beginTest("grid: a surface that first sees a playing clip (entering the mode) does not burst");
        {
            double now = 0.0;
            Rig lp(launchpadStyle(), &now);
            lp.view.clipMask[0] = 0b1; lp.view.playingClip[0] = 0;
            expectEquals(lp.led({ ControlKind::Pad, 0, 0 }).rgb, look.layerRgb[0]);
            expect(! lp.model.isAnimating());
        }

        beginTest("a refused press changes no light");
        for (const auto& d : allSurfaces())
        {
            Rig r(d);
            const auto before = r.model.render();
            r.sink.accept = false;
            r.model.handle(Rig::press({ ControlKind::Top, 3, 0 }));
            r.model.handle(Rig::press({ ControlKind::Pad, 0, 0 }));
            expect(before == r.model.render(), d.name);
        }

        beginTest("a LiveView change calls onChanged, and the new frame follows the view");
        for (const auto& d : allSurfaces())
        {
            Rig r(d);
            int calls = 0;
            r.model.onChanged = [&] { ++calls; };
            r.view.otherApp = true;
            r.view.notify();
            expectEquals(calls, 1, d.name);
            expectEquals(r.led({ ControlKind::Top, 0, 0 }).rgb, look.accentRgb, d.name);
            r.model.setMapping(SurfaceMapping(d));
            expectEquals(calls, 2, d.name + ": a new mapping also asks for a render");
        }

        beginTest("the model listens for its lifetime only");
        {
            FakeLiveView view; FakeControlSink sink;
            expectEquals(view.listenerCount(), 0);
            {
                SurfaceModel m(sink, view, SurfaceMapping(mockGrid()), look);
                expectEquals(view.listenerCount(), 1);
            }
            expectEquals(view.listenerCount(), 0);
        }

        beginTest("no device is named in the shared code: a user override changes the press, and PerformanceItem has no light");
        for (const auto& d : allSurfaces())
        {
            Rig r(d);
            SurfaceMapping m(d);
            mu_core::ControlAction item; item.type = T::PerformanceItem; item.index = 1;
            m.setOverride({ ControlKind::Top, 0, 0 }, item);   // the first button now means item 2
            r.model.setMapping(m);
            r.model.handle(Rig::press({ ControlKind::Top, 0, 0 }));
            expectEquals(r.sink.performed[0].index, 1, d.name);
            expectEquals((int) r.led({ ControlKind::Top, 0, 0 }).rgb, 0, d.name + ": PerformanceItem is dark on the surface");
        }
    }
};

static SurfaceModelTests surfaceModelTests;
