// The Arp layer with no processor at all (extraction-plan 4.1, the self-containment proof): this target is
// compiled from mu-core's layer-side sources, the mu-toni-layer library and this file, with no Plugin/ source.
// It builds an Arp, prepares and binds it, renders it with hand-built RenderContexts and compares the stream
// with the one mu-toni-tests records through the real processor (Tests/ArpLayerFixture.h). A layer that
// secretly needed the processor would fail to link here; one the processor fed differently would not match.

#include "Sequencer/Arp.h"
#include "Persistence/ArpParams.h"
#include "Audio/Wavetable/WavetableBank.h"   // mu-core: the shared bank (a service the host owns)
#include "Tests/ArpLayerFixture.h"

namespace
{
    // Just enough of an AudioProcessor to own a parameter tree for one slot of the Arp type.
    struct ParamHost : juce::AudioProcessor
    {
        const juce::String getName() const override          { return "arp-layer-host"; }
        void prepareToPlay(double, int) override              {}
        void releaseResources() override                      {}
        void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
        juce::AudioProcessorEditor* createEditor() override   { return nullptr; }
        bool hasEditor() const override                       { return false; }
        bool acceptsMidi() const override                     { return true; }
        bool producesMidi() const override                    { return false; }
        double getTailLengthSeconds() const override          { return 0.0; }
        int getNumPrograms() override                         { return 1; }
        int getCurrentProgram() override                      { return 0; }
        void setCurrentProgram(int) override                  {}
        const juce::String getProgramName(int) override       { return {}; }
        void changeProgramName(int, const juce::String&) override {}
        void getStateInformation(juce::MemoryBlock&) override {}
        void setStateInformation(const void*, int) override   {}

        static juce::AudioProcessorValueTreeState::ParameterLayout layout()
        {
            juce::AudioProcessorValueTreeState::ParameterLayout l;
            mu_toni::addLayerParams(l, 0);
            mu_toni::addArpGlobalParams(l);
            return l;
        }
        juce::AudioProcessorValueTreeState apvts { *this, nullptr, "ArpLayer", layout() };
    };
}

struct MuToniLayerLibraryTests : juce::UnitTest
{
    MuToniLayerLibraryTests() : juce::UnitTest("mu-Toni Arp layer without a processor", "mu-toni-layer") {}

    void runTest() override
    {
        namespace fx = mu_toni_fixture;
        beginTest("An Arp built, prepared, bound and rendered from hand-built contexts gives the recorded stream");

        ParamHost host;
        mu_wavetable::WavetableBank bank;
        bank.loadFactoryBank();

        mu_toni::Arp arp;
        arp.runner.setBank(&bank);
        arp.runner.setSlotIndex(0, 0);   // salt 0: the deterministic noise the recorded stream used
        arp.insert->setSlotIndex(0);
        arp.prepare(fx::kSampleRate, fx::kBlock);
        arp.bindParams(host.apvts, 0);
        fx::configure(host.apvts, 0);

        // The contexts the shared driver builds for a playing host block of kBlock frames at slot 0.
        const double bps = (fx::kBpm / 60.0) / fx::kSampleRate;
        double ppq = 0.0;
        std::vector<mu_core::MidiEvent> events;
        juce::AudioBuffer<float> out(2, fx::kBlock);
        fx::StreamHash hash;
        for (int b = 0; b < fx::kBlocks; ++b)
        {
            events.clear();
            for (const auto& k : fx::keys())
                if (k.block == b) events.push_back({ 0, 3, { k.status, k.note, k.velocity } });

            auto ctx = mu_core::makeRenderContext();
            ctx.sampleRate        = fx::kSampleRate;
            ctx.blockSize         = fx::kBlock;
            ctx.preparedBlockSize = fx::kBlock;
            ctx.slot              = 0;
            ctx.bpm               = fx::kBpm;
            ctx.startBeat         = ppq;
            ctx.beatsPerSample    = bps;
            ctx.callBeats         = bps * (double) fx::kBlock;
            ctx.playing           = 1;
            ctx.justStarted       = b == 0 ? 1 : 0;
            ctx.midi              = { events.data(), (int32_t) events.size(), 0, 0xFFFF, 0, 0, 0 };

            out.clear();
            arp.render(ctx, out, fx::kBlock);
            hash.add(out, fx::kBlock);
            ppq += (double) fx::kBlock * fx::kBpm / (60.0 * fx::kSampleRate);   // as the host advances it
        }

        logMessage("Arp layer stream hash " + juce::String::toHexString((juce::int64) hash.h));
        expect(hash.h == fx::kRecordedHash, "the layer alone renders the stream the processor records");
        expect(hash.peak > 0.01f && hash.peak < 4.0f, "and it is a real, bounded sound");
    }
};

static MuToniLayerLibraryTests muToniLayerLibraryTests;
