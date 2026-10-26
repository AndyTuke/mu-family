// The recorded Rhythm stream of Tests/RhythmLayerFixture.h through the REAL headless PluginProcessor: rhythm 0's pre-mixer
// channel stream (the approved test-only channel tap) must hash to kRecordedHash. mu-Tronic's tests render the same stream
// from a Rhythm in a slot, where the layer clocks itself; the two together prove that the slot host hands the layer what
// mu-Clid's processor does.

#include "Plugin/PluginProcessor.h"
#include "Tests/RhythmLayerFixture.h"
#include "Tests/GoldenRender.h"          // mu-core: MockHost
#include "Tests/ProcessorSeamHelpers.h"

namespace
{
    // Appends channel 0's stream as the mixer receives it (sized up front: nothing allocates while rendering).
    struct Channel0Tap : MixerEngine::ChannelTap
    {
        explicit Channel0Tap(int frames) { buf.setSize(2, frames); buf.clear(); }
        void capture(int ch, const juce::AudioBuffer<float>& b, int n) noexcept override
        {
            if (ch != 0) return;
            const int len = juce::jmin(n, buf.getNumSamples() - written);
            for (int c = 0; c < 2 && c < b.getNumChannels(); ++c)
                if (len > 0) juce::FloatVectorOperations::copy(buf.getWritePointer(c) + written, b.getReadPointer(c), len);
            written += juce::jmax(0, len);
        }
        juce::AudioBuffer<float> buf;
        int written = 0;
    };

    struct ExposedProcessor : mu_clid::PluginProcessor
    {
        using mu_clid::PluginProcessor::drainDeferredWork;
    };
}

struct MuClidRhythmLayerRecordTests : juce::UnitTest
{
    MuClidRhythmLayerRecordTests() : juce::UnitTest("mu-Clid Rhythm layer stream through the processor", "mu-clid-layer-record") {}

    void runTest() override
    {
        namespace fx = mu_clid_fixture;
        mu_seam_test::TempStorage storage;
        mu_seam_test::ScopedEnvVar deterministic("MU_DETERMINISTIC_NOISE", "1");
        beginTest("Rhythm 0's pre-mixer stream through the real processor is the recorded Rhythm stream");

        ExposedProcessor p;
        mu_golden::MockHost host;
        host.bpm = fx::kBpm;
        p.setPlayHead(&host);
        p.setRateAndBufferSizeDetails(fx::kSampleRate, fx::kBlock);
        p.prepareToPlay(fx::kSampleRate, fx::kBlock);
        p.samples.load(0, fx::writeSample(storage.root));
        expect(! p.samples.isSampleMissing(0), "the fixture sample loaded");
        fx::configure(p.apvts, p.getRhythm(0), 0);
        p.drainDeferredWork();

        Channel0Tap tap(fx::kBlock * fx::kBlocks);
        p.setChannelTapForTest(&tap);
        juce::AudioBuffer<float> buf(2, fx::kBlock);
        for (int b = 0; b < fx::kBlocks; ++b)
        {
            juce::MidiBuffer midi;
            host.timeInSamples = (juce::int64) b * fx::kBlock;
            buf.clear();
            p.processBlock(buf, midi);
            host.ppq += (double) fx::kBlock * fx::kBpm / (60.0 * fx::kSampleRate);
        }
        p.setChannelTapForTest(nullptr);
        p.setPlayHead(nullptr);
        p.releaseResources();

        expectEquals(tap.written, fx::kBlock * fx::kBlocks, "every block reached the mixer");
        fx::StreamHash hash;
        for (int b = 0; b < fx::kBlocks; ++b) hash.add(tap.buf, fx::kBlock, b * fx::kBlock);
        logMessage("Rhythm layer stream hash 0x" + juce::String::toHexString((juce::int64) hash.h));
        expect(hash.h == fx::kRecordedHash, "the processor renders the recorded layer stream");
        expect(hash.peak > 0.01f && hash.peak < 4.0f, "and it is a real, bounded sound (peak " + juce::String(hash.peak) + ")");
    }
};

static MuClidRhythmLayerRecordTests muClidRhythmLayerRecordTests;
