// Clip auto-save in mu-Tronic on the real Published processor with the real layer types: every type's capture is idempotent
// (the poll compares captures, so one that changed by itself would be rewritten forever), an edit of a lit clip is stored,
// and a Published slot swap saves both slots' edits and re-adopts both trackers.

#include "Plugin/PluginProcessor.h"
#include "Tests/ProcessorSeamHelpers.h"

namespace
{
    using mu_core::LayerTypeId;
    using T = mu_core::ControlActionType;

    struct AutoSaveProcessor : mu_tronic::PluginProcessor {};

    mu_core::ControlAction act(T type, int layer = 0, int index = -1)
    {
        mu_core::ControlAction a;
        a.type = type; a.layer = layer; a.index = index;
        return a;
    }

    // The first continuous row of a slot (its full parameter id): any one float parameter of the layer.
    juce::String firstRow(AutoSaveProcessor& p, int slot)
    {
        const auto* info = p.getLayerTypes().find(p.getLayerTypeId(slot));
        const auto tree = p.captureLayerPresetTree(slot);
        if (info == nullptr) return {};
        for (int i = 0; i < tree.getNumChildren(); ++i)
        {
            const auto local = tree.getChild(i).getProperty("id").toString();
            if (local.isEmpty()) continue;
            const auto id = juce::String::charToString((juce::juce_wchar) info->paramPrefixLetter) + juce::String(slot) + "_" + local;
            if (dynamic_cast<juce::AudioParameterFloat*>(p.apvts.getParameter(id)) != nullptr) return id;
        }
        return {};
    }

    void setFirstRow(AutoSaveProcessor& p, int slot, float to)
    {
        if (auto* param = p.apvts.getParameter(firstRow(p, slot))) param->setValueNotifyingHost(to);
    }
}

struct MuTronicClipAutoSaveTests : juce::UnitTest
{
    MuTronicClipAutoSaveTests() : juce::UnitTest("mu-Tronic clip auto-save", "mu-tronic-autosave") {}

    void runTest() override
    {
        captureIsIdempotentPerType();
        editsAreStored();
        slotSwapSavesBothAndReadopts();
    }

    void captureIsIdempotentPerType()
    {
        for (const auto type : { LayerTypeId::Rhythm, LayerTypeId::Pattern, LayerTypeId::Arp, LayerTypeId::Kick,
                                 LayerTypeId::Bass, LayerTypeId::Hat, LayerTypeId::Snare, LayerTypeId::Rumble })
        {
            const juce::String name = mu_core::layerTypeName(type);
            beginTest("mu-Tronic " + name + " capture is idempotent: two captures with no edit between are identical");
            mu_seam_test::TempStorage storage;
            AutoSaveProcessor p;
            expect(p.setLayerType(1, type));
            p.flushPendingAsyncUpdates();
            const auto twice = [&](const char* when)
            {
                const auto first = p.captureLayerPresetTree(1);
                expect(first.isValid(), name + " has a capture " + when);
                expect(first.isEquivalentTo(p.captureLayerPresetTree(1)), name + " two captures are identical " + when);
            };
            twice("at the defaults");
            setFirstRow(p, 1, 0.3f);
            twice("after an edit");
            expect(p.storeClip(1, 0, "A"));
            setFirstRow(p, 1, 0.7f);
            expect(p.storeClip(1, 1, "B"));
            expect(p.launchClip(1, 0));
            p.flushPendingAsyncUpdates();
            twice("after a launch");
            expect(p.captureLayerPresetTree(1).isEquivalentTo(p.getLayer(1)->clips->get(0)),
                   name + " a launched clip captures back as the clip (no feedback rewrite)");
        }
    }

    void editsAreStored()
    {
        beginTest("mu-Tronic auto-save: an edit of a lit Kick clip is stored once, a launch is adopted and not rewritten");
        mu_seam_test::TempStorage storage;
        AutoSaveProcessor p;
        expect(p.setLayerType(0, LayerTypeId::Kick));
        p.flushPendingAsyncUpdates();
        setFirstRow(p, 0, 0.2f);
        p.storeClip(0, 2, "two");
        setFirstRow(p, 0, 0.8f);
        expect(p.perform(act(T::StoreClip, 0, 1)));
        for (int i = 0; i < 3; ++i)
            expect(! p.stepClipAutoSave(), "unchanged");

        setFirstRow(p, 0, 0.4f);
        expect(! p.stepClipAutoSave());
        expect(p.stepClipAutoSave(), "settled edit stored");
        expect(p.getLayer(0)->clips->get(1).isEquivalentTo(p.captureLayerPresetTree(0)));
        for (int i = 0; i < 3; ++i)
            expect(! p.stepClipAutoSave());

        const auto two = p.getLayer(0)->clips->get(2);
        expect(p.perform(act(T::LaunchClip, 0, 2)));
        for (int i = 0; i < 4; ++i)
            expect(! p.stepClipAutoSave(), "the launch is adopted, not written");
        expect(p.getLayer(0)->clips->get(2).isEquivalentTo(two));
    }

    void slotSwapSavesBothAndReadopts()
    {
        beginTest("a Published slot swap flushes both slots' edits into their banks and re-adopts both trackers");
        mu_seam_test::TempStorage storage;
        AutoSaveProcessor p;
        expect(p.setLayerType(0, LayerTypeId::Kick));
        expect(p.setLayerType(2, LayerTypeId::Arp));
        p.flushPendingAsyncUpdates();
        for (const int slot : { 0, 2 })
        {
            setFirstRow(p, slot, 0.3f);
            expect(p.perform(act(T::StoreClip, slot, slot + 1)));   // slot 0 -> clip 1, slot 2 -> clip 3, both lit
            setFirstRow(p, slot, 0.6f);                              // an edit the poll has not stored yet
        }
        expect(p.swapLayers(0, 2));
        p.flushPendingAsyncUpdates();
        expectEquals(p.getLiveState().getPlayingClip(2), 1, "the Kick's lit clip followed it to slot 2");
        expectEquals(p.getLiveState().getPlayingClip(0), 3, "and the Arp's to slot 0");
        // A Track capture carries its slot index (the "i" of its step row child), which a swap changes without any edit: compare without it.
        const auto same = [](juce::ValueTree a, juce::ValueTree b)
        {
            a = a.createCopy(); b = b.createCopy();
            for (auto* t : { &a, &b })
                for (int c = 0; c < t->getNumChildren(); ++c)
                {
                    auto child = t->getChild(c);
                    child.removeProperty("i", nullptr);
                }
            return a.isEquivalentTo(b);
        };
        expect(same(p.getLayer(2)->clips->get(1), p.captureLayerPresetTree(2)), "the Kick's late edit was saved");
        expect(same(p.getLayer(0)->clips->get(3), p.captureLayerPresetTree(0)), "the Arp's late edit was saved");
        for (int i = 0; i < 4; ++i)
            expect(! p.stepClipAutoSave(), "nothing is rewritten after the swap");

        // Two lit layers share the poll, one per step: a settled edit of slot 0 is written within two rounds.
        setFirstRow(p, 0, 0.1f);
        int writes = 0;
        for (int i = 0; i < 4; ++i)
            writes += p.stepClipAutoSave() ? 1 : 0;
        expectEquals(writes, 1, "the tracker works on the new slot, and writes once");
        expect(same(p.getLayer(0)->clips->get(3), p.captureLayerPresetTree(0)));
    }
};

static MuTronicClipAutoSaveTests muTronicClipAutoSaveTests;
