// Tests of mu-Host: the mu-Tronic shell built with ONE slot (MU_MAX_LAYERS=1, no buses), on the REAL mu_host::PluginProcessor, headless.
// One slot that starts Empty, every engine the editor's drop-down offers, saved state under mu-Host's own names, and a rendering audio
// thread that survives engine changes.

#include "../Plugin/PluginProcessor.h"
#include "../License/EngineLicensing.h"
#include "Tests/ProcessorSeamHelpers.h"

#include <cmath>

// The product classes name their editors in a virtual, defined in PluginEditor.cpp, which this headless target does not link.
juce::AudioProcessorEditor* mu_tronic::PluginProcessor::createEditor() { return nullptr; }
juce::AudioProcessorEditor* mu_host::PluginProcessor::createEditor() { return nullptr; }

namespace
{
    static_assert(mu_limits::kMaxLayers == 1, "mu-host compiles with MU_MAX_LAYERS=1");
    static_assert(mu_limits::kMaxBuses == 0, "mu-host has no buses");

    struct HostProcessor : mu_host::PluginProcessor
    {
        bool isStandaloneApp() const noexcept override { return true; }   // a headless run is not a plug-in host
    };

    // Renders `blocks` blocks of 256 frames with a held note on channel 1; returns false when any sample is not finite or is huge.
    bool renderSane(HostProcessor& p, int blocks)
    {
        juce::AudioBuffer<float> buf(2, 256);
        juce::MidiBuffer midi;
        for (int b = 0; b < blocks; ++b)
        {
            buf.clear();
            midi.clear();
            if (b == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 48, (juce::uint8) 100), 0);
            p.processBlock(buf, midi);
            if (! mu_seam_test::bufferIsSane(buf, 50.0f)) return false;
        }
        return true;
    }

    // The engines the editor's drop-down lists (Plugin/PluginEditor.cpp).
    const mu_core::LayerTypeId kEngines[] = {
        mu_core::LayerTypeId::Rhythm, mu_core::LayerTypeId::Pattern, mu_core::LayerTypeId::Arp, mu_core::LayerTypeId::Kick,
        mu_core::LayerTypeId::Bass, mu_core::LayerTypeId::Hat, mu_core::LayerTypeId::Snare, mu_core::LayerTypeId::Rumble,
        mu_core::LayerTypeId::FmPerc, mu_core::LayerTypeId::Chord, mu_core::LayerTypeId::Phrase };
}

struct MuHostTests : juce::UnitTest
{
    MuHostTests() : juce::UnitTest("mu-Host one-slot processor", "mu-host") {}

    void runTest() override
    {
        beginTest("one Empty slot, its own name and product slug, no scenes or pages");
        {
            mu_seam_test::TempStorage storage;
            HostProcessor p;
            expectEquals(p.getNumLayers(), 1);
            expect(p.getLayerTypeId(0) == mu_core::LayerTypeId::Empty);
            expectEquals(p.getProductSlug(), juce::String("mu-host"));
            expect(p.getName().endsWithIgnoreCase("-Host"), "name " + p.getName());
            expect(! p.offersSceneControls() && ! p.offersLayerPages());
            expect(p.apvts.state.getType() == juce::Identifier("MuHostState"));
        }

        beginTest("every engine on the drop-down can take the slot and renders finite audio, one after another");
        {
            mu_seam_test::TempStorage storage;
            HostProcessor p;
            p.prepareToPlay(48000.0, 256);
            p.toggleInternalPlay();
            for (auto type : kEngines)
            {
                expect(p.setLayerType(0, type), "set type " + juce::String((int) type));
                expect(p.getLayerTypeId(0) == type);
                expect(renderSane(p, 6), "sane output for type " + juce::String((int) type));
            }
        }

        beginTest("the engine and its rows survive a saved-state round trip, under mu-Host's own state tag");
        {
            mu_seam_test::TempStorage storage;
            HostProcessor p;
            expect(p.setLayerType(0, mu_core::LayerTypeId::Kick));
            juce::MemoryBlock block;
            p.getStateInformation(block);
            expect(block.getSize() > 0);
            HostProcessor q;
            q.setStateInformation(block.getData(), (int) block.getSize());
            expect(q.getLayerTypeId(0) == mu_core::LayerTypeId::Kick, "the restored slot is a Kick");
            expect(q.apvts.state.getType() == juce::Identifier("MuHostState"));
        }

        beginTest("opening an engine runs its own product's licence check (no key here, so unlicensed), and a product's key is for the whole product");
        {
            mu_seam_test::TempStorage storage;
            HostProcessor p;
            expect(! p.hasLicensing(), "no engine, no licensing");
            auto checkAs = [&](mu_core::LayerTypeId type, const char* productId, const char* urlPart, int price)
            {
                const auto name = p.licenseEngine(type);
                expect(name.isNotEmpty(), "a product name for engine " + juce::String((int) type));
                expect(p.hasLicensing() && ! p.isSupporter(), juce::String(productId) + " is unlicensed without its key");
                expectEquals(p.getLicensingProductKey(), juce::String(productId));
                expect(p.getCheckoutUrl().contains(urlPart), juce::String(productId) + " opens its own checkout page");
                expectEquals(p.getSuggestedPrice(), price);
            };
            checkAs(mu_core::LayerTypeId::Kick,    "mu-On",    "3dd813d9", 20);
            checkAs(mu_core::LayerTypeId::Rumble,  "mu-On",    "3dd813d9", 20);   // another engine of the same product: the same licence
            checkAs(mu_core::LayerTypeId::Rhythm,  "mu-Clid",  "12d18415", 20);
            checkAs(mu_core::LayerTypeId::Pattern, "mu-Tant",  "a0354b49", 20);
            checkAs(mu_core::LayerTypeId::Arp,     "mu-Toni",  "dbecc900", 20);
            checkAs(mu_core::LayerTypeId::Phrase,  "mu-Tilate", "1121cfce", 20);
            p.licenseEngine(mu_core::LayerTypeId::Empty);
            expect(! p.hasLicensing() && p.isLicensed(), "an Empty slot has no licensing and no prompt");
        }

        beginTest("the engine list offers only engines of licensed products (none here), and an Empty slot belongs to no product");
        {
            mu_seam_test::TempStorage storage;
            const auto dir = juce::File::createTempFile("muHostLic");
            for (const auto type : { mu_core::LayerTypeId::Rhythm, mu_core::LayerTypeId::Kick, mu_core::LayerTypeId::Phrase,
                                     mu_core::LayerTypeId::Empty })
                expect(! mu_host::isEngineLicensed(type, dir, "muHost"), "no key, so not offered");
        }

        beginTest("changing the engine while the audio thread renders is safe");
        {
            mu_seam_test::TempStorage storage;
            HostProcessor p;
            p.prepareToPlay(48000.0, 256);
            p.toggleInternalPlay();
            bool ok = true;
            for (int round = 0; round < 3 && ok; ++round)
                for (auto type : kEngines)
                {
                    p.setLayerType(0, type);
                    ok = ok && renderSane(p, 2);
                }
            expect(ok, "no insane block while the engine changed");
        }
    }
};

static MuHostTests muHostTests;
