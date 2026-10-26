// What the mu-Tronic editor's scene pads, clip strip and Notes In control read from the processor (docs/mu-tronic/design-ui.md
// 3.7, 3.8 and 4.8): which types read notes and which keep a clip bank, that the surface is offered the scene picker, and
// that a scene launch is reported (which clip, on how many layers) with the aggregate the pads and the picker light from.
// The editor itself is not built headless; these are the rules it applies, on the real processor with the real layer types.

#include "Plugin/PluginProcessor.h"
#include "Control/SceneState.h"
#include "Tests/ProcessorSeamHelpers.h"

namespace
{
    using mu_core::LayerTypeId;

    struct UiProcessor : mu_tronic::PluginProcessor
    {
        bool isStandaloneApp() const noexcept override { return true; }
    };

    // The first continuous row of a slot, so a clip's contents can be told apart without knowing the type's rows.
    void nudge(UiProcessor& p, int slot, float to)
    {
        const auto* info = p.getLayerTypes().find(p.getLayerTypeId(slot));
        const auto tree = p.captureLayerPresetTree(slot);
        if (info == nullptr) return;
        for (int i = 0; i < tree.getNumChildren(); ++i)
        {
            const auto local = tree.getChild(i).getProperty("id").toString();
            if (local.isEmpty()) continue;
            const auto id = juce::String::charToString((juce::juce_wchar) info->paramPrefixLetter) + juce::String(slot) + "_" + local;
            if (dynamic_cast<juce::AudioParameterFloat*>(p.apvts.getParameter(id)) != nullptr)
            {
                p.apvts.getParameter(id)->setValueNotifyingHost(to);
                return;
            }
        }
    }
}

struct MuTronicSceneUiTests : juce::UnitTest
{
    MuTronicSceneUiTests() : juce::UnitTest("mu-Tronic editor rules: Notes In types, clip bank types, scene reports", "mu-tronic-scenes") {}

    void runTest() override
    {
        beginTest("Notes In is shown for the types that read notes only: Arp and Pattern; Rhythm and the Track types do not");
        {
            mu_seam_test::TempStorage storage;
            UiProcessor p;
            const auto& types = p.getLayerTypes();
            auto takes = [&](LayerTypeId id) { const auto* i = types.find(id); return i != nullptr && i->takesNotes; };
            expect(takes(LayerTypeId::Arp));
            expect(takes(LayerTypeId::Pattern));
            for (auto id : { LayerTypeId::Rhythm, LayerTypeId::Kick, LayerTypeId::Bass, LayerTypeId::Hat, LayerTypeId::Snare, LayerTypeId::Rumble, LayerTypeId::FmPerc, LayerTypeId::Chord })
                expect(! takes(id), juce::String(mu_core::layerTypeName(id)) + " ignores notes");
        }

        beginTest("every type keeps a clip bank except Rumble, which processes another layer's audio");
        {
            mu_seam_test::TempStorage storage;
            UiProcessor p;
            const auto& types = p.getLayerTypes();
            for (auto id : { LayerTypeId::Rhythm, LayerTypeId::Pattern, LayerTypeId::Arp, LayerTypeId::Kick, LayerTypeId::Bass, LayerTypeId::Hat, LayerTypeId::Snare, LayerTypeId::FmPerc, LayerTypeId::Chord })
            {
                const auto* i = types.find(id);
                expect(i != nullptr && i->hasClipBank, juce::String(mu_core::layerTypeName(id)) + " has a clip bank");
            }
            const auto* rumble = types.find(LayerTypeId::Rumble);
            expect(rumble != nullptr && ! rumble->hasClipBank, "Rumble has none");
        }

        beginTest("the surface is offered the scene picker, and a fresh instance has launched no scene");
        {
            mu_seam_test::TempStorage storage;
            UiProcessor p;
            expect(p.getLiveView()->hasSceneControls());
            expect(p.getLiveView()->hasLayerPages(), "and the page buttons: sixteen slots, two pages");
            expectEquals(p.getLiveView()->getPageCount(), 2);
            expectEquals(p.getLiveView()->getShownPage(), 0);
            const auto launch = p.getLiveView()->getLastSceneLaunch();
            expectEquals((int) launch.serial, 0);
            expectEquals(launch.clip, -1);
        }

        beginTest("a scene launch is reported with its clip and the layers it reached; the aggregate counts only clips a layer would take");
        {
            mu_seam_test::TempStorage storage;
            UiProcessor p;
            const int slots[] = { 0, 2, 3, 5 };
            const LayerTypeId mix[] = { LayerTypeId::Kick, LayerTypeId::Arp, LayerTypeId::Rumble, LayerTypeId::Pattern };
            for (int i = 0; i < 4; ++i) expect(p.setLayerType(slots[i], mix[i]));
            p.flushPendingAsyncUpdates();
            for (int i = 0; i < 4; ++i)
            {
                nudge(p, slots[i], 0.2f);
                expect(p.storeClip(slots[i], 0, "low"));
                nudge(p, slots[i], 0.8f);
                expect(p.storeClip(slots[i], 1, "high"));
            }
            p.clearClip(slots[3], 1);   // the Pattern has no clip 2
            // A Kick's clip put into the Arp's bank by hand: stored, but the Arp would not take it.
            p.getLayer(slots[1])->clips->store(2, p.getLayer(slots[0])->clips->get(0), "alien");

            const auto& view = *p.getLiveView();
            expect(! view.isClipForLayer(slots[1], 2), "the Arp will not take a Kick's clip");
            expect(view.isClipForLayer(slots[1], 0) && view.isClipForLayer(slots[0], 0));
            expect(view.isClipForLayer(5, 7), "an empty clip slot raises no objection");

            auto info = [&](int clip) { return mu_core::sceneInfo(view, clip); };
            expect(info(0).state == mu_core::SceneState::Stored && info(0).layers == 4, "scene 1: four layers store it, none plays it (yet)");
            expect(info(1).layers == 3, "scene 2: the Pattern has none");
            expect(info(2).state == mu_core::SceneState::Empty && info(2).layers == 0, "scene 3: its only clip is foreign, so nobody would take it");
            expect(info(6).state == mu_core::SceneState::Empty, "scene 7: nobody stores it");

            expect(p.launchScene(1));   // stopped: applied at once
            auto launch = view.getLastSceneLaunch();
            expectEquals(launch.clip, 1);
            expectEquals(launch.layers, 3, "reached the Kick, the Arp and the Rumble");
            expectEquals((int) launch.serial, 1);
            expect(info(1).state == mu_core::SceneState::Playing, "every layer that stores scene 2 plays it");
            expect(info(0).state == mu_core::SceneState::Stored, "scene 1 is stored, not playing");

            expect(! p.launchScene(6));
            launch = view.getLastSceneLaunch();
            expectEquals(launch.clip, 6);
            expectEquals(launch.layers, 0, "an empty scene reaches nobody, and is still reported");
            expectEquals((int) launch.serial, 2);

            expect(p.launchScene(1), "a layer already playing the clip counts as reached");
            expectEquals(view.getLastSceneLaunch().layers, 3);
            expectEquals((int) view.getLastSceneLaunch().serial, 3);
            p.flushPendingAsyncUpdates();
        }
    }
};

static MuTronicSceneUiTests sMuTronicSceneUiTests;
