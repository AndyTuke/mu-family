// The recorded Track streams of Tests/TrackLayerFixture.h through the REAL headless PluginProcessor: channels 0
// and 1's pre-mixer streams (the approved test-only channel tap) must hash to kRecordedHash. The same streams
// are rendered with no processor at all by mu-on-layer-tests (LayerLibraryTests.cpp); the two together prove
// that the processor hands the layers exactly the contract's RenderContext (and the Rumble its peer) and nothing
// else.

#include "Plugin/PluginProcessor.h"
#include "Tests/TrackLayerFixture.h"
#include "Tests/GoldenRender.h"          // mu-core: MockHost
#include "Tests/ProcessorSeamHelpers.h"

namespace
{
    // Appends channels 0 and 1's streams as the mixer receives them (sized up front: nothing allocates while rendering).
    struct TwoChannelTap : MixerEngine::ChannelTap
    {
        explicit TwoChannelTap(int frames) { for (auto& b : buf) { b.setSize(2, frames); b.clear(); } }
        void capture(int ch, const juce::AudioBuffer<float>& b, int n) noexcept override
        {
            if (ch < 0 || ch > 1) return;
            auto& dst = buf[(size_t) ch];
            const int len = juce::jmin(n, dst.getNumSamples() - written[(size_t) ch]);
            for (int c = 0; c < 2 && c < b.getNumChannels(); ++c)
                if (len > 0) juce::FloatVectorOperations::copy(dst.getWritePointer(c) + written[(size_t) ch], b.getReadPointer(c), len);
            written[(size_t) ch] += juce::jmax(0, len);
        }
        std::array<juce::AudioBuffer<float>, 2> buf;
        std::array<int, 2> written {};
    };
}

struct MuOnTrackLayerRecordTests : juce::UnitTest
{
    MuOnTrackLayerRecordTests() : juce::UnitTest("mu-On Track layer streams through the processor", "mu-on-layer-record") {}

    void runTest() override
    {
        namespace fx = mu_on_fixture;
        mu_seam_test::TempStorage storage;
        beginTest("Channels 0 and 1's pre-mixer streams through the real processor are the recorded Track streams");

        mu_on::PluginProcessor p;
        p.addLayer();
        mu_golden::MockHost host;
        host.bpm = fx::kBpm;
        p.setPlayHead(&host);
        p.setRateAndBufferSizeDetails(fx::kSampleRate, fx::kBlock);
        p.prepareToPlay(fx::kSampleRate, fx::kBlock);
        fx::configure(p.apvts);
        fx::configureRow(p.getTrack(0));

        TwoChannelTap tap(fx::kBlock * fx::kBlocks);
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

        expectEquals(tap.written[0], fx::kBlock * fx::kBlocks, "every block reached the mixer");
        fx::StreamHash hash;
        for (int b = 0; b < fx::kBlocks; ++b)
            for (int t = 0; t < 2; ++t) hash.add(tap.buf[(size_t) t], fx::kBlock, b * fx::kBlock);
        logMessage("Track layer stream hash " + juce::String::toHexString((juce::int64) hash.h));
        expect(hash.h == fx::kRecordedHash, "the processor renders the recorded layer streams");
        expect(hash.peak > 0.01f && hash.peak < 4.0f, "and they are real, bounded sounds");

        // One track alone per engine: the streams mu-Tronic's locked Kick, Bass, Hat and Snare layers must reproduce.
        beginTest("Channel 0's stream for each non-Rumble engine alone is the recorded single-track stream");
        for (int e = 0; e < 4; ++e)
        {
            const auto engine = mu_on::engineFromIndex(e);
            mu_seam_test::TempStorage storage2;
            mu_seam_test::ScopedEnvVar deterministic("MU_DETERMINISTIC_NOISE", "1");
            mu_on::PluginProcessor q;
            mu_golden::MockHost host2;
            host2.bpm = fx::kBpm;
            q.setPlayHead(&host2);
            q.setRateAndBufferSizeDetails(fx::kSampleRate, fx::kBlock);
            q.prepareToPlay(fx::kSampleRate, fx::kBlock);
            fx::configureSingle(q.apvts, engine);
            fx::configureRow(q.getTrack(0));

            TwoChannelTap tap2(fx::kBlock * fx::kBlocks);
            q.setChannelTapForTest(&tap2);
            juce::AudioBuffer<float> buf2(2, fx::kBlock);
            for (int b = 0; b < fx::kBlocks; ++b)
            {
                juce::MidiBuffer midi;
                host2.timeInSamples = (juce::int64) b * fx::kBlock;
                buf2.clear();
                q.processBlock(buf2, midi);
                host2.ppq += (double) fx::kBlock * fx::kBpm / (60.0 * fx::kSampleRate);
            }
            q.setChannelTapForTest(nullptr);
            q.setPlayHead(nullptr);
            q.releaseResources();

            fx::StreamHash single;
            for (int b = 0; b < fx::kBlocks; ++b) single.add(tap2.buf[0], fx::kBlock, b * fx::kBlock);
            logMessage(juce::String(mu_on::engineName(engine)) + " single-track stream hash " + juce::String::toHexString((juce::int64) single.h));
            expect(single.h == fx::kSingleHash[e], juce::String(mu_on::engineName(engine)) + " renders its recorded single-track stream");
            expect(single.peak > 0.01f && single.peak < 4.0f, "and it is a real, bounded sound");
        }
    }
};

static MuOnTrackLayerRecordTests muOnTrackLayerRecordTests;
