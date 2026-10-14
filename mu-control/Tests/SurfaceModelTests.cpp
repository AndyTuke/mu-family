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

        beginTest("a top press performs PerformanceItem(n) on every surface");
        for (const auto& d : allSurfaces())
        {
            Rig r(d);
            expect(r.model.handle(Rig::press({ ControlKind::Top, 2, 0 })), d.name);
            expectEquals((int) r.sink.performed.size(), 1);
            expect(r.sink.performed[0].type == T::PerformanceItem && r.sink.performed[0].index == 2, d.name);
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

        beginTest("top row: current bright, available dim, missing dim red, queued pulsing, empty off");
        for (const auto& d : allSurfaces())
        {
            Rig r(d);
            r.view.queued = 3;
            const int n = d.buttonCount(ButtonKind::Top);
            expectEquals(r.led({ ControlKind::Top, 0, 0 }).rgb, look.accentRgb, d.name + ": current");
            expectEquals(r.led({ ControlKind::Top, 1, 0 }).rgb, scaled(look.accentRgb, look.dimScale), d.name + ": available");
            expectEquals(r.led({ ControlKind::Top, 2, 0 }).rgb, scaled(look.faultRgb, look.dimScale), d.name + ": missing file");
            expect(r.led({ ControlKind::Top, 3, 0 }).rgb != 0, d.name + ": queued is lit");
            if (n > 4) expectEquals((int) r.led({ ControlKind::Top, 4, 0 }).rgb, 0, d.name + ": empty item");
        }

        beginTest("dim is exactly dimScale times bright, for the item and the layer colours");
        for (const auto& d : allSurfaces())
        {
            Rig r(d);
            const auto bright = r.led({ ControlKind::Top, 0, 0 }).rgb;
            const auto dim    = r.led({ ControlKind::Top, 1, 0 }).rgb;
            expectEquals(dim, scaled(bright, look.dimScale), d.name);
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
            r.model.handle(Rig::press({ ControlKind::Top, 1, 0 }));
            expect(before == r.model.render(), d.name);
        }

        beginTest("a LiveView change calls onChanged, and the new frame follows the view");
        for (const auto& d : allSurfaces())
        {
            Rig r(d);
            int calls = 0;
            r.model.onChanged = [&] { ++calls; };
            r.view.current = 1;
            r.view.notify();
            expectEquals(calls, 1, d.name);
            expectEquals(r.led({ ControlKind::Top, 1, 0 }).rgb, look.accentRgb, d.name);
            expectEquals(r.led({ ControlKind::Top, 0, 0 }).rgb, scaled(look.accentRgb, look.dimScale), d.name);
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

        beginTest("queued pulses by the surface: animated colours (RGB), native mode, or a steady middle level");
        {
            double now = 0.0;
            Rig lp(launchpadStyle(), &now);
            lp.view.queued = 1;
            const auto dim = scaled(look.accentRgb, look.dimScale);
            expectEquals(lp.led({ ControlKind::Top, 1, 0 }).rgb, dim, "RGB pulse starts at dim");
            now = 250.0;   // half a period at 2 Hz
            expectEquals(lp.led({ ControlKind::Top, 1, 0 }).rgb, look.accentRgb, "and peaks at bright");
            expect(lp.led({ ControlKind::Top, 1, 0 }).mode == LedMode::Static, "RGB surfaces always get static colours");
            now = 125.0;   // quarter period: halfway
            const auto mid = lp.led({ ControlKind::Top, 1, 0 }).rgb;
            expectEquals(mid, scaled(look.accentRgb, look.dimScale + (1.0f - look.dimScale) * 0.5f));
            now = 500.0;   // a full period later: back at dim
            expectEquals(lp.led({ ControlKind::Top, 1, 0 }).rgb, dim);

            Rig nb(nativeBlink());
            nb.view.queued = 1;
            const auto l = nb.led({ ControlKind::Top, 1, 0 });
            expect(l.mode == LedMode::Pulse && l.rgb == look.accentRgb, "no RGB but a native pulse: the device blinks it");

            Rig mock(mockGrid());
            mock.view.queued = 1;
            const auto m = mock.led({ ControlKind::Top, 1, 0 });
            expect(m.mode == LedMode::Static, "no pulse at all: steady");
            expectEquals(m.rgb, scaled(look.accentRgb, 0.5f * (1.0f + look.dimScale)));
            expect(m.rgb != look.accentRgb && m.rgb != scaled(look.accentRgb, look.dimScale), "distinct from current and available");
        }

        beginTest("the blink animation runs only while an RGB light pulses");
        {
            Rig lp(launchpadStyle());
            expect(! lp.model.isAnimating(), "nothing queued");
            lp.view.queued = 2;
            expect(lp.model.isAnimating(), "a queued item pulses");
            lp.view.queued = -1;
            expect(! lp.model.isAnimating());
            lp.view.queued = 2;
            lp.view.mode = false;
            expect(! lp.model.isAnimating(), "dark outside the mode");

            Rig nb(nativeBlink());
            nb.view.queued = 1;
            expect(! nb.model.isAnimating(), "the device animates natively, so no timer is needed");
            Rig mock(mockGrid());
            mock.view.queued = 1;
            expect(! mock.model.isAnimating());
        }

        beginTest("a queued slot that holds no item lights nothing");
        {
            Rig r(launchpadStyle());
            r.view.queued = 5;   // slots 4 to 7 are empty
            expectEquals((int) r.led({ ControlKind::Top, 5, 0 }).rgb, 0);
            expect(! r.model.isAnimating(), "an unlit queue does not start the blink timer");
        }

        beginTest("no device is named in the shared code: a user override changes both the press and the light");
        for (const auto& d : allSurfaces())
        {
            Rig r(d);
            SurfaceMapping m(d);
            mu_core::ControlAction item; item.type = T::PerformanceItem; item.index = 1;
            m.setOverride({ ControlKind::Top, 0, 0 }, item);   // the first button now means item 2
            r.model.setMapping(m);
            r.model.handle(Rig::press({ ControlKind::Top, 0, 0 }));
            expectEquals(r.sink.performed[0].index, 1, d.name);
            expectEquals(r.led({ ControlKind::Top, 0, 0 }).rgb, scaled(look.accentRgb, look.dimScale), d.name + ": it lights as item 2 (available)");
        }
    }
};

static SurfaceModelTests surfaceModelTests;
