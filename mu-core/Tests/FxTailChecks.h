#pragma once

// The FX-tail check every composed product runs against its REAL PluginProcessor (headless): a full-preset
// switch performed mid-bar through ControlAction PerformanceItem must not cut the global FX tail when it
// commits. Two full presets share identical FX settings and send level; B has every layer muted. The
// control render plays A through the bar; the switched render performs B mid-bar and lets it commit at
// the bar. The source is muted just before the bar in both runs, so only the reverb tail is left ringing
// when the commit lands, and the two outputs must agree there.
//
// This runs on one thread: the commit happens between blocks, so it proves the tail state survives the
// commit but cannot see a block skipped by a host that honours suspendProcessing.

#include "Tests/ProcessorSeamHelpers.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

namespace mu_fxtail_test
{
    struct Render
    {
        std::vector<float> left, right;   // every rendered frame
        int  commitBlock   = -1;          // first block after which no full preset was pending (switched run)
        double commitPpq   = 0.0;         // host position at the end of that block
        bool pendingAfterPerform = false; // the switch was staged, not applied at once
        int  currentItem   = -1;          // live-state item at the end
    };

    // Plays the bar line at ppq 12 at 120 bpm; blockSize frames per block at 48 kHz.
    template <typename Proc>
    Render renderRun(const std::function<void(Proc&)>& makeAudible, int blockSize, bool doSwitch, int forcedBlocks)
    {
        using namespace mu_seam_test;
        using Type = mu_core::ControlActionType;
        constexpr double kRate = 48000.0, kSwitchPpq = 10.0, kMutePpq = 11.7, kBarPpq = 12.0;

        const auto act = [](Type type, int layer, int index)
        {
            mu_core::ControlAction a;
            a.type = type; a.layer = layer; a.index = index;
            return a;
        };

        TempStorage storage;
        juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Standalone);
        auto p = std::make_unique<Proc>();
        juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Undefined);

        makeAudible(*p);
        const int layers = p->getNumLayers();
        const auto setLayers = [&](const char* suffix, float v)
        {
            for (int i = 0; i < layers; ++i) setParam(p->apvts, "ch" + juce::String(i) + suffix, v);
        };

        // Identical FX in both presets: a long reverb fed hard by every layer.
        setParam(p->apvts, "rev_en", 1.0f);
        setParam(p->apvts, "rev_size", 0.9f);
        setParam(p->apvts, "rev_lvl", 1.0f);
        setLayers("_sendRev", 1.0f);
        setLayers("_mute", 0.0f);

        const auto dir = p->getFullPresetDir();
        dir.createDirectory();
        const auto ext = "." + p->getFullPresetExtension();
        const auto fileA = dir.getChildFile("fxA" + ext), fileB = dir.getChildFile("fxB" + ext);
        p->saveFullPresetTo(fileA);
        setLayers("_mute", 1.0f);
        p->saveFullPresetTo(fileB);
        setLayers("_mute", 0.0f);

        mu_pp::PerformanceFile list;
        list.setItem(0, p->getProductStateId(), fileA, dir, "A");
        list.setItem(1, p->getProductStateId(), fileB, dir, "B");
        const auto gig = dir.getChildFile("fx.muPerformance");
        list.save(gig);
        p->loadPerformance(list, gig);
        p->perform(act(Type::PerformanceMode, 0, 1));
        p->perform(act(Type::PerformanceItem, 0, 0));   // stopped: A is live at once

        p->prepareToPlay(kRate, blockSize);
        RunningPlayHead host(kRate, blockSize);
        host.ppq = kBarPpq - 4.0;
        p->setPlayHead(&host);

        Render r;
        juce::AudioBuffer<float> buf(2, blockSize);
        juce::MidiBuffer midi;
        bool switched = false, muted = false, committed = false;
        for (int b = 0; forcedBlocks > 0 ? b < forcedBlocks : b < 60000; ++b)
        {
            if (doSwitch && ! switched && host.ppq >= kSwitchPpq)
            {
                p->perform(act(Type::PerformanceItem, 0, 1));
                r.pendingAfterPerform = p->hasPendingFullPreset();
                switched = true;
            }
            if (! muted && host.ppq >= kMutePpq) { setLayers("_mute", 1.0f); muted = true; }

            buf.clear();
            midi.clear();
            p->processBlock(buf, midi);
            p->flushPendingAsyncUpdates();
            for (int i = 0; i < blockSize; ++i) { r.left.push_back(buf.getSample(0, i)); r.right.push_back(buf.getSample(1, i)); }

            if (switched && ! committed && ! p->hasPendingFullPreset())
            {
                committed = true;
                r.commitBlock = b;
                r.commitPpq = host.ppq;
            }
            // Switched run: end one second of tail after the commit (the control is then told this length).
            if (forcedBlocks <= 0 && committed && b >= r.commitBlock + (int) (kRate / blockSize) + 1)
                break;
        }
        r.currentItem = p->getLiveState().getCurrentItem();
        p->setPlayHead(nullptr);
        p->releaseResources();
        return r;
    }

    template <typename Proc>
    void runFxTailChecks(juce::UnitTest& t, const std::function<void(Proc&)>& makeAudible)
    {
        for (const int blockSize : { 64, 512 })
        {
            t.beginTest("a full-preset commit mid-bar keeps the FX tail, block size " + juce::String(blockSize));

            const auto sw = renderRun<Proc>(makeAudible, blockSize, true, 0);
            t.expect(sw.pendingAfterPerform, "the switch is staged until the bar, not applied at once");
            t.expect(sw.commitBlock >= 0, "the staged full preset commits (no longer pending)");
            t.expectEquals(sw.currentItem, 1, "the current performance item is B");
            if (sw.commitBlock < 0) continue;
            t.expect(sw.commitPpq >= 11.9, "the commit does not land before the bar (ppq " + juce::String(sw.commitPpq, 3) + ")");

            const int total = (int) (sw.left.size() / (size_t) blockSize);
            const auto ctl = renderRun<Proc>(makeAudible, blockSize, false, total);
            t.expectEquals((int) ctl.left.size(), (int) sw.left.size(), "both runs render the same length");

            // The window: the frames after the commit block, up to one second of tail.
            const size_t first = (size_t) (sw.commitBlock + 1) * (size_t) blockSize;
            const size_t last = std::min(sw.left.size(), ctl.left.size());
            if (first >= last) { t.expect(false, "a tail window exists after the commit"); continue; }

            double refEnergy = 0.0, diffEnergy = 0.0, ctlStep = 0.0, swStep = 0.0;
            size_t zeroRun = 0, longestZeroRun = 0;
            for (size_t i = first; i < last; ++i)
            {
                const double cl = ctl.left[i], cr = ctl.right[i], sl = sw.left[i], sr = sw.right[i];
                refEnergy  += cl * cl + cr * cr;
                diffEnergy += (sl - cl) * (sl - cl) + (sr - cr) * (sr - cr);
                if (i > first)
                {
                    ctlStep = std::max({ ctlStep, std::abs(cl - ctl.left[i - 1]), std::abs(cr - ctl.right[i - 1]) });
                    swStep  = std::max({ swStep,  std::abs(sl - sw.left[i - 1]),  std::abs(sr - sw.right[i - 1]) });
                }
                zeroRun = (sl == 0.0 && sr == 0.0) ? zeroRun + 1 : 0;
                longestZeroRun = std::max(longestZeroRun, zeroRun);
            }
            const double n = (double) (last - first) * 2.0;
            const double refRms = std::sqrt(refEnergy / n);
            const double diffDb = 10.0 * std::log10((diffEnergy + 1.0e-30) / (refEnergy + 1.0e-30));
            const double clickBound = 3.0 * ctlStep + 1.0e-4;

            t.logMessage("block " + juce::String(blockSize) + ": commit block " + juce::String(sw.commitBlock)
                         + " at ppq " + juce::String(sw.commitPpq, 3) + ", tail rms " + juce::String(refRms, 6)
                         + ", switched-vs-control " + juce::String(diffDb, 1) + " dB, longest exact-zero run "
                         + juce::String((int) longestZeroRun) + " frames, max step " + juce::String(swStep, 6)
                         + " (control " + juce::String(ctlStep, 6) + ")");

            t.expect(refRms > 1.0e-4, "the control tail is audible, so the comparison means something");
            t.expect(diffDb < -60.0, "the switched tail matches the control to better than -60 dB (" + juce::String(diffDb, 1) + " dB)");
            t.expect(longestZeroRun < 16, "no run of exact zeros inside the tail (" + juce::String((int) longestZeroRun) + " frames)");
            t.expect(swStep <= clickBound, "no click at the commit: largest step " + juce::String(swStep, 6)
                                           + " <= " + juce::String(clickBound, 6));
        }
    }
}
