#pragma once

// The clip store / launch checks every composed product runs against its REAL PluginProcessor
// (headless). `Proc` is the product's processor with layerSwapper exposed (`using
// ProcessorBase::layerSwapper;`). The product supplies one float parameter of layer 0 to change and
// read back; everything else is the shared ProcessorBase API.

#include "Tests/ProcessorSeamHelpers.h"

#include <functional>
#include <memory>

namespace mu_clip_test
{
    template <typename Proc>
    struct Accessor
    {
        std::function<void(Proc&, float)> set;
        std::function<float(Proc&)>       get;
        float a, b, tolerance;   // two distinct values of the parameter
    };

    // The live half of the clip bank on the REAL processor: pads launch through the live state's actions
    // (Performance mode is standalone-only, so these processors are built as standalones).
    template <typename Proc>
    void runLiveClipChecks(juce::UnitTest& t, const Accessor<Proc>& x)
    {
        using mu_seam_test::TempStorage;
        using Type = mu_core::ControlActionType;

        const auto act = [](Type type, int layer = 0, int index = -1)
        {
            mu_core::ControlAction a;
            a.type = type; a.layer = layer; a.index = index;
            return a;
        };
        // A standalone processor with a one-item performance loaded and the mode on (layer 0 selected).
        const auto makeLive = [&](TempStorage& storage)
        {
            juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Standalone);
            auto p = std::make_unique<Proc>();
            juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Undefined);
            mu_pp::PerformanceFile list;
            list.setItem(0, p->getProductStateId(), storage.root.getChildFile("p.preset"), storage.root, "P");
            p->loadPerformance(list, storage.root.getChildFile("live.muPerformance"));
            t.expect(p->perform(act(Type::PerformanceMode, 0, 1)), "Performance mode is on");
            return p;
        };

        t.beginTest("live clips: a stopped launch plays at once, a playing one queues and lands; the pad lights follow");
        {
            TempStorage storage;
            auto p = makeLive(storage);
            const auto* view = p->getLiveView();
            x.set(*p, x.a); p->storeClip(0, 1, "A");
            x.set(*p, x.b); p->storeClip(0, 2, "B");
            t.expectEquals((int) view->getClipMask(0), 0b110, "storing publishes the mask");

            t.expect(! p->perform(act(Type::LaunchClip, 0, 5)), "an empty pad does nothing");
            t.expect(p->perform(act(Type::LaunchClip, 0, 1)));
            t.expectEquals(view->getPlayingClip(0), 1, "stopped: playing at once");
            t.expectEquals(view->getQueuedClip(0), -1);
            t.expectEquals(p->getLayer(0)->activeClip, 1, "and recorded with the layer for the session");
            t.expectWithinAbsoluteError(x.get(*p), x.a, x.tolerance);

            p->layerSwapper.flagBoundaries(true, 0.0, 0.1);   // the transport is running
            t.expect(p->perform(act(Type::LaunchClip, 0, 2)));
            t.expectEquals(view->getQueuedClip(0), 2);
            t.expectEquals(view->getPlayingClip(0), 1, "playing moves only at the commit");
            t.expect(p->hasPendingSwap(0));
            t.expect(p->layerSwapper.flagBoundaries(true, 3.95, 0.2), "the bar line flags it");
            p->layerSwapper.commit();
            p->getLiveState().reconcile();
            t.expectEquals(view->getPlayingClip(0), 2);
            t.expectEquals(view->getQueuedClip(0), -1);
            t.expectEquals(p->getLayer(0)->activeClip, 2);
            t.expectWithinAbsoluteError(x.get(*p), x.b, x.tolerance);

            const auto mute = [&] { return p->getLiveView()->isLayerMuted(0); };
            t.expect(! mute());
            t.expect(p->perform(act(Type::LaunchClip, 0, 2)), "the playing pad mutes");
            t.expect(mute());
            t.expect(p->perform(act(Type::LaunchClip, 0, 2)), "and unmutes");
            t.expect(! mute());

            p->perform(act(Type::LaunchClip, 0, 1));
            t.expectEquals(view->getQueuedClip(0), 1);
            t.expect(p->perform(act(Type::LaunchClip, 0, 2)), "the playing pad with 1 queued");
            t.expectEquals(view->getQueuedClip(0), -1, "cancels the queue");
            t.expect(! p->hasPendingSwap(0), "and the staged launch");
            t.expect(! mute(), "without muting");
        }

        t.beginTest("live clips: a layer-preset load or a cancel supersedes a queued clip; clearing a queued clip cancels it");
        {
            TempStorage storage;
            auto p = makeLive(storage);
            const auto* view = p->getLiveView();
            x.set(*p, x.a); p->saveLayerPreset(0, "live");
            const auto files = p->slotPresetFiles(0);
            t.expectEquals(files.size(), 1);
            p->storeClip(0, 1); p->storeClip(0, 2);
            p->perform(act(Type::LaunchClip, 0, 1));
            t.expectEquals(view->getPlayingClip(0), 1);

            p->layerSwapper.flagBoundaries(true, 0.0, 0.1);
            p->perform(act(Type::LaunchClip, 0, 2));
            t.expectEquals(view->getQueuedClip(0), 2);
            p->loadLayerPreset(0, files.getFirst());   // a plain layer preset replaces the staged clip
            t.expectEquals(view->getQueuedClip(0), -1);
            t.expectEquals(view->getPlayingClip(0), -1, "the layer is no longer playing any clip");
            t.expectEquals(p->getLayer(0)->activeClip, -1);
            p->layerSwapper.flagBoundaries(true, 3.95, 0.2); p->layerSwapper.commit(); p->getLiveState().reconcile();
            t.expectEquals(view->getPlayingClip(0), -1, "the load that landed is not a clip");

            p->perform(act(Type::LaunchClip, 0, 1));
            t.expectEquals(view->getQueuedClip(0), 1);
            p->cancelPendingSwap(0);
            p->getLiveState().reconcile();
            t.expectEquals(view->getQueuedClip(0), -1, "a cancelled launch clears the lamp");

            p->perform(act(Type::LaunchClip, 0, 2));
            t.expectEquals(view->getQueuedClip(0), 2);
            t.expect(p->clearClip(0, 2));
            t.expectEquals(view->getQueuedClip(0), -1, "clearing the queued clip drops the launch");
            t.expect(! p->hasPendingSwap(0), "and cancels the staged payload");
            t.expectEquals((int) view->getClipMask(0), 0b10);

            t.expect(p->perform(act(Type::ClipNext)), "ClipNext from nothing queues the first stored clip");
            t.expectEquals(view->getQueuedClip(0), 1);
            t.expect(! p->perform(act(Type::ClipPrev)), "no other stored clip");

            p->cancelPendingSwap(0);
            p->layerSwapper.stageFull(juce::ValueTree("Full"));
            t.expect(! p->perform(act(Type::LaunchClip, 0, 1)), "a launch is refused while a full preset is staged");
        }

        t.beginTest("live clips: removing and swapping layers carries the playing clip and the mask with the layer");
        {
            TempStorage storage;
            auto p = makeLive(storage);
            if (! p->canEditLayers()) return;
            while (p->getNumLayers() < 2) p->addLayer();
            const auto* view = p->getLiveView();
            p->storeClip(1, 3); p->storeClip(1, 4);
            t.expect(p->perform(act(Type::LaunchClip, 1, 4)));
            t.expectEquals(view->getPlayingClip(1), 4);

            t.expect(p->removeLayer(0));
            t.expectEquals(view->getPlayingClip(0), 4, "the old layer 2 is now layer 1 and still plays its clip");
            t.expectEquals((int) view->getClipMask(0), (1 << 3) | (1 << 4));
            t.expectEquals(view->getPlayingClip(p->getNumLayers()), -1);

            t.expect(p->addLayer() >= 0);
            t.expectEquals((int) view->getClipMask(1), 0, "an added layer starts with no clips");
            t.expect(p->swapLayers(0, 1));
            t.expectEquals(view->getPlayingClip(1), 4, "a swap moves the playing clip with its layer");
            t.expectEquals(view->getPlayingClip(0), -1);
            t.expectEquals((int) view->getClipMask(1), (1 << 3) | (1 << 4));
            t.expectEquals((int) view->getClipMask(0), 0);

            p->resetLayer(1);
            t.expectEquals(view->getPlayingClip(1), -1, "a reset layer plays no clip");
            t.expectEquals((int) view->getClipMask(1), 0);
        }
    }

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

        runLiveClipChecks<Proc>(t, x);
    }
}
