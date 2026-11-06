// The Phrase layer type in a mu-Tronic slot: it is offered by the registry, its p{N}_ rows exist only for a slot that holds
// one, a loop loads through the type's services (worker decode and detection, publish on the message thread), a saved session
// restores the loop with its slices, and a slot that holds another type answers the services with nothing.

#include "Plugin/PluginProcessor.h"
#include "Sequencer/PhraseSlotHost.h"
#include "Tests/ProcessorSeamHelpers.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

namespace
{
    using mu_core::LayerTypeId;

    // Four loud bursts half a second apart, two seconds of mono 16-bit audio.
    juce::File writeBursts(const juce::File& dir)
    {
        const double sr = 48000.0;
        const int frames = 96000;
        juce::AudioBuffer<float> b(1, frames);
        b.clear();
        juce::Random rng(5);
        for (int t = 0; t < 4; ++t)
            for (int i = 0; i < (int) (0.3 * sr); ++i)
                b.setSample(0, t * 24000 + i, 0.8f * std::exp(-(float) i / (0.03f * (float) sr)) * (rng.nextFloat() * 2.0f - 1.0f));
        auto f = dir.getChildFile("bursts.wav");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream(f.createOutputStream());
        auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions {}.withSampleRate(sr).withNumChannels(1).withBitsPerSample(16));
        if (writer != nullptr) writer->writeFromAudioSampleBuffer(b, 0, frames);
        return f;
    }

    template <class Fn>
    bool pumpUntil(Fn&& done, int ms = 15000)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
        while (juce::Time::getMillisecondCounter() < end)
        {
            if (done()) return true;
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        }
        return done();
    }

    mu_tilate::PhraseSlotHost* phraseServices(mu_tronic::PluginProcessor& p)
    {
        return dynamic_cast<mu_tilate::PhraseSlotHost*>(p.findLayerServices(LayerTypeId::Phrase));
    }
}

struct MuTronicPhraseTests : juce::UnitTest
{
    MuTronicPhraseTests() : juce::UnitTest("mu-Tronic Phrase layer type", "mu-tronic-phrase") {}

    void runTest() override
    {
        beginTest("the registry offers Phrase with the p prefix and mu-Tilate's preset files");
        {
            mu_tronic::PluginProcessor p;
            const auto* info = p.getLayerTypes().find(LayerTypeId::Phrase);
            expect(info != nullptr);
            if (info == nullptr) return;
            expectEquals(juce::String(info->name), juce::String("Phrase"));
            expectEquals((int) info->paramPrefixLetter, (int) 'p');
            expectEquals(juce::String(info->presetExtension), juce::String(".muPhrase"));
            expect(p.apvts.getParameter("p3_gain") != nullptr);
        }

        beginTest("a loop loads into a Phrase slot through the services, and a session restores it");
        {
            mu_seam_test::TempStorage temp;
            const auto file = writeBursts(temp.root);
            juce::MemoryBlock saved;
            {
                mu_tronic::PluginProcessor p;
                expect(p.setLayerType(2, LayerTypeId::Phrase));
                auto* host = phraseServices(p);
                expect(host != nullptr);
                if (host == nullptr) return;
                host->loadLoopAsync(2, file);
                expect(pumpUntil([&] { return host->getPhrase(2).getData() != nullptr; }));
                const auto d = host->getPhrase(2).getData();
                expect(d != nullptr && d->slices.size() >= 3);
                p.getStateInformation(saved);
            }
            {
                mu_tronic::PluginProcessor q;
                q.setStateInformation(saved.getData(), (int) saved.getSize());
                expect(q.getLayer(2) != nullptr && q.getLayer(2)->typeId() == (int) LayerTypeId::Phrase);
                auto* host = phraseServices(q);
                expect(host != nullptr);
                if (host == nullptr) return;
                // The restore names the loop by path; the services' timer (or this call) brings it in.
                host->reloadMissingLoops();
                expect(pumpUntil([&] { return host->getPhrase(2).getData() != nullptr; }));
            }
        }

        beginTest("a slot that holds another type answers with nothing");
        {
            mu_tronic::PluginProcessor p;
            expect(p.setLayerType(0, LayerTypeId::Phrase));
            expect(p.setLayerType(1, LayerTypeId::Arp));
            auto* host = phraseServices(p);
            expect(host != nullptr);
            if (host == nullptr) return;
            expectEquals(host->getModLive(1, 0), 0.0f);
            expectEquals(host->getPreviewPlayhead(1), -1.0);
            expect(p.apvts.getParameter("p1_gain") != nullptr);   // the rows exist in the layout either way
        }
    }
};

static MuTronicPhraseTests muTronicPhraseTests;
