// The recorded Pattern stream of Tests/PatternLayerFixture.h through the REAL headless PluginProcessor: layer 0's
// pre-mixer channel stream (the approved test-only channel tap) must hash to kRecordedHash. The same stream is
// rendered with no processor at all by mu-tant-layer-tests (LayerLibraryTests.cpp); the two together prove that
// the processor hands the layer exactly the contract's RenderContext and nothing else.

#include "Plugin/PluginProcessor.h"
#include "Tests/PatternLayerFixture.h"
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
}

struct MuTantPatternLayerRecordTests : juce::UnitTest
{
    MuTantPatternLayerRecordTests() : juce::UnitTest("mu-Tant Pattern layer stream through the processor", "mu-tant-layer-record") {}

    void runTest() override
    {
        namespace fx = mu_tant_fixture;
        mu_seam_test::TempStorage storage;
        mu_seam_test::ScopedEnvVar deterministic("MU_DETERMINISTIC_NOISE", "1");
        beginTest("Layer 0's pre-mixer stream through the real processor is the recorded Pattern stream");

        mu_tant::PluginProcessor p;
        mu_golden::MockHost host;
        host.bpm = fx::kBpm;
        p.setPlayHead(&host);
        p.setRateAndBufferSizeDetails(fx::kSampleRate, fx::kBlock);
        p.prepareToPlay(fx::kSampleRate, fx::kBlock);
        p.setMidiNoteMode(1);
        fx::configure(p.apvts, p.getPattern(0), 0);

        Channel0Tap tap(fx::kBlock * fx::kBlocks);
        p.setChannelTapForTest(&tap);
        juce::AudioBuffer<float> buf(2, fx::kBlock);
        for (int b = 0; b < fx::kBlocks; ++b)
        {
            juce::MidiBuffer midi;
            for (const auto& k : fx::keys())
                if (k.block == b) midi.addEvent(juce::MidiMessage(k.status, k.note, k.velocity), 0);
            host.timeInSamples = (juce::int64) b * fx::kBlock;
            buf.clear();
            p.processBlock(buf, midi);
            host.ppq += (double) fx::kBlock * fx::kBpm / (60.0 * fx::kSampleRate);
        }
        p.setChannelTapForTest(nullptr);
        p.setPlayHead(nullptr);
        p.releaseResources();
        p.setMidiNoteMode(0);   // the setting is saved per machine: leave it as the next test expects

        expectEquals(tap.written, fx::kBlock * fx::kBlocks, "every block reached the mixer");
        fx::StreamHash hash;
        for (int b = 0; b < fx::kBlocks; ++b) hash.add(tap.buf, fx::kBlock, b * fx::kBlock);
        logMessage("Pattern layer stream hash " + juce::String::toHexString((juce::int64) hash.h));
        expect(hash.h == fx::kRecordedHash, "the processor renders the recorded layer stream");
        expect(hash.peak > 0.01f && hash.peak < 4.0f, "and it is a real, bounded sound");
    }
};

static MuTantPatternLayerRecordTests muTantPatternLayerRecordTests;
