// The Track layer with no processor at all (extraction-plan 4.1, the self-containment proof): this target is
// compiled from mu-core's layer-side sources, the mu-on-layer library and this file, with no Plugin/ source.
// It builds two Tracks, binds and prepares them, renders them with hand-built RenderContexts (track 1, a Rumble,
// reads track 0's block through a hand-built peer table, as the driver's priority pass does) and compares the
// streams with the ones mu-on-tests records through the real processor (Tests/TrackLayerFixture.h). A layer
// that secretly needed the processor would fail to link here; one the processor fed differently would not match.

#include "Sequencer/Track.h"
#include "Persistence/TrackParams.h"
#include "Tests/TrackLayerFixture.h"

#include <array>

namespace
{
    // Just enough of an AudioProcessor to own a parameter tree for two Track slots and the groove rows.
    struct ParamHost : juce::AudioProcessor
    {
        const juce::String getName() const override          { return "track-layer-host"; }
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
            mu_on::addLayerParams(l, 0);
            mu_on::addLayerParams(l, 1);
            return l;
        }
        juce::AudioProcessorValueTreeState apvts { *this, nullptr, "TrackLayer", layout() };
    };
}

struct MuOnLayerLibraryTests : juce::UnitTest
{
    MuOnLayerLibraryTests() : juce::UnitTest("mu-On Track layer without a processor", "mu-on-layer") {}

    void runTest() override
    {
        namespace fx = mu_on_fixture;
        beginTest("Two Tracks built, bound, prepared and rendered from hand-built contexts give the recorded streams");

        ParamHost host;
        fx::configure(host.apvts);
        std::array<mu_on::Track, 2> tracks;
        for (int s = 0; s < 2; ++s)
        {
            tracks[(size_t) s].slot = s;
            tracks[(size_t) s].bindParams(host.apvts, s);
            tracks[(size_t) s].prepare(fx::kSampleRate, fx::kBlock);
            tracks[(size_t) s].completeSetup(host.apvts, s);   // the host's message thread builds the engine the slot names
        }
        fx::configureRow(tracks[0]);
        expectEquals(tracks[0].renderPriority(), 0);
        expectEquals(tracks[1].renderPriority(), 1, "the Rumble renders after its peers");

        // The contexts the shared driver builds for a playing host block of kBlock frames.
        const double bps = (fx::kBpm / 60.0) / fx::kSampleRate;
        double ppq = 0.0;
        std::array<juce::AudioBuffer<float>, 2> out { juce::AudioBuffer<float>(2, fx::kBlock), juce::AudioBuffer<float>(2, fx::kBlock) };
        std::array<mu_core::PeerTap, mu_limits::kMaxLayers> peers {};
        fx::StreamHash hash;
        for (int b = 0; b < fx::kBlocks; ++b)
        {
            auto ctx = mu_core::makeRenderContext();
            ctx.sampleRate        = fx::kSampleRate;
            ctx.blockSize         = fx::kBlock;
            ctx.preparedBlockSize = fx::kBlock;
            ctx.bpm               = fx::kBpm;
            ctx.startBeat         = ppq;
            ctx.beatsPerSample    = bps;
            ctx.callBeats         = bps * (double) fx::kBlock;
            ctx.playing           = 1;
            ctx.justStarted       = b == 0 ? 1 : 0;

            // Priority 0, then the peer table, then priority 1 (design 2.1.1, phases 1 to 3).
            auto c0 = ctx;
            c0.slot = 0;
            out[0].clear();
            tracks[0].render(c0, out[0], fx::kBlock);
            peers[0] = { { out[0].getReadPointer(0), out[0].getReadPointer(1) }, 1, 0 };
            auto c1 = ctx;
            c1.slot = 1;
            c1.peers = peers.data();
            c1.numPeers = mu_limits::kMaxLayers;
            out[1].clear();
            tracks[1].render(c1, out[1], fx::kBlock);

            for (int t = 0; t < 2; ++t) hash.add(out[(size_t) t], fx::kBlock);
            ppq += (double) fx::kBlock * fx::kBpm / (60.0 * fx::kSampleRate);   // as the host advances it
        }

        logMessage("Track layer stream hash " + juce::String::toHexString((juce::int64) hash.h));
        expect(hash.h == fx::kRecordedHash, "the layers alone render the streams the processor records");
        expect(hash.peak > 0.01f && hash.peak < 4.0f, "and they are real, bounded sounds");
    }
};

static MuOnLayerLibraryTests muOnLayerLibraryTests;
