#pragma once

// The clip auto-save checks every product runs against its REAL PluginProcessor (headless). The one that matters
// most is capture idempotence: the poll compares the layer's capture with the last one, so a capture that is not a
// pure function of the live state (a clock, a counter, an unordered container) would be rewritten forever.

#include "Tests/ClipLaunchChecks.h"

namespace mu_clip_test
{
    // Capturing twice with no edit between gives the same tree, after each kind of change a clip goes through.
    template <typename Proc>
    void runCaptureIdempotenceChecks(juce::UnitTest& t, const Accessor<Proc>& x, const juce::String& product)
    {
        using mu_seam_test::TempStorage;
        t.beginTest(product + " layer capture is idempotent: two captures with no edit between are identical");
        TempStorage storage;
        Proc p;
        const auto twice = [&](const char* when)
        {
            const auto first = p.captureLayerPresetTree(0);
            const auto second = p.captureLayerPresetTree(0);
            t.expect(first.isValid(), juce::String("a capture exists ") + when);
            t.expect(first.isEquivalentTo(second), juce::String("two captures are identical ") + when);
        };
        twice("at the defaults");
        x.set(p, x.a);
        twice("after an edit");
        t.expect(p.storeClip(0, 1, "A"));
        twice("after a store");
        x.set(p, x.b);
        t.expect(p.storeClip(0, 2, "B"));
        t.expect(p.launchClip(0, 1));
        twice("after a launch");
        t.expect(p.launchClip(0, 2));
        twice("after a second launch");
        t.expect(p.captureLayerPresetTree(0).isEquivalentTo(p.getLayer(0)->clips->get(2)),
                 "a launched clip captures back as the clip (no feedback rewrite)");
    }

    // An edit is stored once, a launch is adopted and never rewritten, on the real capture.
    template <typename Proc>
    void runAutoSaveChecks(juce::UnitTest& t, const Accessor<Proc>& x, const juce::String& product)
    {
        using mu_seam_test::TempStorage;
        using Type = mu_core::ControlActionType;
        const auto act = [](Type type, int layer, int index)
        {
            mu_core::ControlAction a;
            a.type = type; a.layer = layer; a.index = index;
            return a;
        };

        t.beginTest(product + " auto-save: an edit of the lit clip is stored, a launch is not rewritten");
        TempStorage storage;
        Proc p;
        x.set(p, x.a);
        p.storeClip(0, 2, "two");
        x.set(p, x.b);
        t.expect(p.perform(act(Type::StoreClip, 0, 1)));   // clip 1 stored and lit
        for (int i = 0; i < 3; ++i)
            t.expect(! p.stepClipAutoSave(), "nothing changed, nothing is written");

        x.set(p, x.a);
        const auto stored = p.getLayer(0)->clips->get(1);
        t.expect(! p.stepClipAutoSave());
        t.expect(p.stepClipAutoSave(), "the settled edit is stored");
        t.expect(! p.getLayer(0)->clips->get(1).isEquivalentTo(stored));
        t.expect(p.getLayer(0)->clips->get(1).isEquivalentTo(p.captureLayerPresetTree(0)));
        for (int i = 0; i < 3; ++i)
            t.expect(! p.stepClipAutoSave(), "and not again");

        const auto two = p.getLayer(0)->clips->get(2);
        t.expect(p.perform(act(Type::LaunchClip, 0, 2)));
        for (int i = 0; i < 4; ++i)
            t.expect(! p.stepClipAutoSave(), "the launch is adopted, not written");
        t.expect(p.getLayer(0)->clips->get(2).isEquivalentTo(two), "the launched clip is unchanged");
        t.expectWithinAbsoluteError(x.get(p), x.a, x.tolerance);
    }
}
