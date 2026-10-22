// mu-On's five layer types over the one Track class (docs/mu-tronic/design.md 1.4): the LayerTypeInfo
// constants, the type of a track and of a saved track preset, and the engine lock create applies.

#include "Plugin/PluginProcessor.h"
#include "Sequencer/TrackTypes.h"
#include "Tests/ProcessorSeamHelpers.h"

class TrackTypesTest : public juce::UnitTest
{
public:
    TrackTypesTest() : juce::UnitTest("mu-On layer types", "mu-on-processor") {}

    void runTest() override
    {
        using namespace mu_on;
        const mu_core::LayerTypeInfo* infos[] = { &kKickType, &kBassType, &kHatType, &kSnareType, &kRumbleType, &kFmPercType, &kChordType };

        beginTest("seven types, one per engine: id, name, group, the shared prefix t and extension .muTrack");
        for (int e = 0; e < kNumEngineTypes; ++e)
        {
            const auto engine = engineFromIndex(e);
            const auto& info = *infos[e];
            expect(info.id == layerTypeOf(engine), "type id of " + juce::String(engineName(engine)));
            expectEquals(juce::String(info.name), juce::String(engineName(engine)));
            expectEquals(juce::String(mu_core::layerTypeName(info.id)), juce::String(engine == EngineType::FmPerc ? "FmPerc" : engineName(engine)), "saved by the engine's name (FM Perc is saved as FmPerc)");
            expect(engineOfType(info.id) == engine, "the type runs its engine");
            expectEquals(juce::String(info.group), juce::String("mu-On"));
            expect(info.paramPrefixLetter == 't');
            expectEquals(juce::String(info.presetExtension), juce::String(".muTrack"));
            expect(info.addLayerParams == &addLayerParams && info.create != nullptr, "one table and one class for all seven");
            const auto layer = info.create(3, info.id);
            const auto* track = dynamic_cast<const Track*>(layer.get());
            expect(track != nullptr && track->slot == 3, "create builds a Track for the slot");
            expect(track != nullptr && track->engineLocked && track->engineSetting() == engine, "create locks the track to its type's engine");
            expectEquals(juce::String(info.contentFolderName), juce::String("muOn"));
            expectEquals(juce::String(info.presetFolder), juce::String("Tracks"));
            expect(juce::String(info.typeTag).isEmpty() && info.appColourId == 0, "the look (tag, paint colour) is the slot host's to add");
            expect(info.typeFromPreset == &typeFromPreset, "the shared extension is told apart by the saved engine");
            expect(track != nullptr && track->retirePolicy().kind == mu_core::RetirePolicy::Kind::Fade, "every Track type fades");
            expectEquals(track->retirePolicy().fadeMs, 25.0f);
            expectEquals(track->retirePolicy().maxSeconds, 2.0f);
        }
        expect(! isTrackType(mu_core::LayerTypeId::Arp) && ! isTrackType(mu_core::LayerTypeId::Empty));

        beginTest("a track's type follows its engine; a track preset carries its type (and a lane-era preset too)");
        {
            mu_seam_test::TempStorage storage;
            PluginProcessor p;
            for (int i = 1; i < 5; ++i) p.addLayer();   // Kick, Bass, Hat, Snare, Rumble
            for (int i = 0; i < 5; ++i)
            {
                expectEquals(p.getTrack(i).typeId(), (int) layerTypeOf(engineFromIndex(i)));
                expect(typeFromPreset(p.captureLayerPresetTree(i)) == layerTypeOf(engineFromIndex(i)), "preset of track " + juce::String(i));
            }
            p.selectTrackEngine(0, EngineType::Snare);
            expectEquals(p.getTrack(0).typeId(), (int) mu_core::LayerTypeId::Snare, "the type changes with the engine");

            juce::ValueTree legacy("MuOnTrack");
            legacy.setProperty("lane", "Hat", nullptr);
            expect(typeFromPreset(legacy) == mu_core::LayerTypeId::Hat, "a preset from the fixed lanes names its instrument");
            expect(typeFromPreset(juce::ValueTree("MuOnTrack")) == mu_core::LayerTypeId::Empty, "a tree naming nothing is Empty");
        }
    }
};

static TrackTypesTest trackTypesTest;
