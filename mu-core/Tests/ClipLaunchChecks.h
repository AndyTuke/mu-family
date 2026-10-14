#pragma once

// The clip store / launch checks every composed product runs against its REAL PluginProcessor
// (headless). `Proc` is the product's processor with layerSwapper exposed (`using
// ProcessorBase::layerSwapper;`). The product supplies one float parameter of layer 0 to change and
// read back; everything else is the shared ProcessorBase API.

#include "Tests/ProcessorSeamHelpers.h"

#include <functional>

namespace mu_clip_test
{
    template <typename Proc>
    struct Accessor
    {
        std::function<void(Proc&, float)> set;
        std::function<float(Proc&)>       get;
        float a, b, tolerance;   // two distinct values of the parameter
    };

    template <typename Proc>
    void runClipLaunchChecks(juce::UnitTest& t, const Accessor<Proc>& x)
    {
        using mu_seam_test::TempStorage;

        t.beginTest("storeClip, change, launchClip (stopped) restores the layer; the bank, name and colour are untouched");
        {
            TempStorage storage;
            Proc p;
            p.getLayer(0)->name = "keep";
            p.getLayer(0)->colourIndex = 5;
            x.set(p, x.a);
            const auto before = p.captureLayerPresetTree(0);
            t.expect(before.isValid());
            t.expect(p.storeClip(0, 3, "A"));
            t.expectEquals((int) p.clipMask(0), 1 << 3);

            x.set(p, x.b);
            const auto stored = p.getLayer(0)->clips->get(3);
            t.expect(p.launchClip(0, 3));
            t.expectWithinAbsoluteError(x.get(p), x.a, x.tolerance);
            t.expect(p.captureLayerPresetTree(0).isEquivalentTo(before), "capture then stage round-trips the layer");
            t.expect(p.getLayer(0)->clips->get(3).isEquivalentTo(stored), "the launch did not write the bank");
            t.expectEquals(p.getLayer(0)->clips->getName(3), juce::String("A"));
            t.expect(p.getLayer(0)->name == "keep" && p.getLayer(0)->colourIndex == 5, "identity untouched");
        }

        t.beginTest("a clip stored from a layer-preset file launches exactly as loading that file; a layer-preset load clears activeClip");
        {
            TempStorage storage;
            Proc src;
            x.set(src, x.a);
            src.saveLayerPreset(0, "clipsrc");
            const auto files = src.slotPresetFiles(0);
            t.expectEquals(files.size(), 1);

            Proc viaClip, viaLoad;
            x.set(viaClip, x.b);
            x.set(viaLoad, x.b);
            t.expect(viaClip.loadClipFromFile(0, 6, files.getFirst()));
            t.expect(viaClip.launchClip(0, 6));
            viaLoad.loadLayerPreset(0, files.getFirst());
            t.expectWithinAbsoluteError(x.get(viaClip), x.a, x.tolerance);
            t.expect(viaClip.captureLayerPresetTree(0).isEquivalentTo(viaLoad.captureLayerPresetTree(0)),
                     "the launch and the direct load leave the same layer");

            viaClip.getLayer(0)->activeClip = 6;
            viaClip.loadLayerPreset(0, files.getFirst());
            t.expectEquals(viaClip.getLayer(0)->activeClip, -1);
            t.expectEquals((int) viaClip.clipMask(0), 1 << 6, "the load leaves the bank alone");
        }

        t.beginTest("loadClipFromFile rejects a wrong tag and an unreadable file with onLoadError, bank unchanged");
        {
            TempStorage storage;
            Proc p;
            p.storeClip(0, 1, "kept");
            juce::StringArray errors;
            p.onLoadError = [&errors](const juce::String& e) { errors.add(e); };

            auto dir = storage.root.getChildFile("clipfiles");
            dir.createDirectory();
            const auto wrong = dir.getChildFile("wrong.xml");
            wrong.replaceWithText("<NotThisProduct/>");
            const auto junk = dir.getChildFile("junk.xml");
            junk.replaceWithText("not xml");
            t.expect(! p.loadClipFromFile(0, 1, wrong));
            t.expect(! p.loadClipFromFile(0, 1, junk));
            t.expect(! p.loadClipFromFile(0, 1, dir.getChildFile("missing.xml")));
            t.expectEquals(errors.size(), 3);
            t.expectEquals((int) p.clipMask(0), 1 << 1);
            t.expectEquals(p.getLayer(0)->clips->getName(1), juce::String("kept"));
        }

        t.beginTest("clearClip, an empty clip, a bad index and a staged full preset");
        {
            TempStorage storage;
            Proc p;
            p.storeClip(0, 0); p.storeClip(0, 5);
            p.getLayer(0)->activeClip = 5;
            t.expect(p.clearClip(0, 5));
            t.expectEquals((int) p.clipMask(0), 1);
            t.expectEquals(p.getLayer(0)->activeClip, -1);
            t.expect(! p.launchClip(0, 5), "a cleared clip is empty");
            t.expect(! p.launchClip(0, 8) && ! p.launchClip(0, -1) && ! p.storeClip(0, 8));

            x.set(p, x.b);
            p.layerSwapper.stageFull(juce::ValueTree("Full"));
            t.expect(! p.launchClip(0, 0), "a full preset is staged");
            t.expectWithinAbsoluteError(x.get(p), x.b, x.tolerance);
        }

        t.beginTest("launch while playing is pending and lands at the boundary, firing onLayerPresetCommitted");
        {
            TempStorage storage;
            Proc p;
            x.set(p, x.a);
            p.storeClip(0, 2);
            x.set(p, x.b);
            int committed = -1;
            p.onLayerPresetCommitted = [&committed](int s) { committed = s; };

            p.layerSwapper.flagBoundaries(true, 0.0, 0.1);   // the transport is running
            t.expect(p.launchClip(0, 2));
            t.expect(p.hasPendingSwap(0));
            t.expectWithinAbsoluteError(x.get(p), x.b, x.tolerance);
            t.expect(p.layerSwapper.flagBoundaries(true, 3.95, 0.2), "the bar line flags it");
            p.layerSwapper.commit();
            t.expect(! p.hasPendingSwap(0));
            t.expectWithinAbsoluteError(x.get(p), x.a, x.tolerance);
            t.expectEquals(committed, 0);
        }
    }
}
