// Track::render on the audio path, outside the processor's block driver: each engine at every call size, a
// Rumble reading its source through RenderContext::peer (and silence for an absent, priority-1 or removed
// source), the engine swap a parameter change makes (built on the calling thread, never inside render), and
// the RumbleEngine's own tail handling (cleared by reset, skipped once a silent feed has let it die away).
// Formerly GrooveVoicesTests: the per-slot engine owner is gone, each Track holds its one engine.

#include "Plugin/PluginProcessor.h"
#include "Tests/ProcessorSeamHelpers.h"
#include "Tests/AllocationProbe.h"   // mu-core: heap activity inside a scope

namespace
{
    using namespace mu_on;

    float rmsOf(const juce::AudioBuffer<float>& b, int n)
    {
        double sum = 0.0;
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = 0; i < n; ++i) { const double v = b.getReadPointer(c)[i]; sum += v * v; }
        return (float) std::sqrt(sum / juce::jmax(1, n * b.getNumChannels()));
    }

    bool allFinite(const juce::AudioBuffer<float>& b, int n)
    {
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = 0; i < n; ++i)
                if (! std::isfinite(b.getReadPointer(c)[i])) return false;
        return true;
    }

    // A decaying low burst standing in for a kick.
    void fillBurst(juce::AudioBuffer<float>& b, int n)
    {
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = 0; i < n; ++i)
                b.setSample(c, i, std::sin(0.02f * (float) i) * std::exp(-0.004f * (float) i));
    }

    // A playing call of `n` frames for `slot`, starting at absolute frame `frame` (120 BPM).
    mu_core::RenderContext callAt(int slot, juce::int64 frame, int n, double sr, int prepared,
                                  const mu_core::PeerTap* peers = nullptr)
    {
        auto c = mu_core::makeRenderContext();
        c.sampleRate = sr;
        c.blockSize = n;
        c.preparedBlockSize = prepared;
        c.slot = slot;
        c.bpm = 120.0;
        c.beatsPerSample = 2.0 / sr;
        c.startBeat = (double) frame * c.beatsPerSample;
        c.callBeats = (double) n * c.beatsPerSample;
        c.playing = 1;
        c.peers = peers;
        c.numPeers = peers != nullptr ? mu_limits::kMaxLayers : 0;
        return c;
    }
}

class TrackRenderTest : public juce::UnitTest
{
public:
    TrackRenderTest() : juce::UnitTest("mu-on Track render", "Engine") {}

    void runTest() override
    {
        using namespace mu_seam_test;
        constexpr double sr = 48000.0;
        constexpr int    prepared = 512;

        beginTest("every engine renders finite audio at call sizes 1, 64 and 512, and the Kick sounds on its step");
        {
            TempStorage storage;
            PluginProcessor p;
            for (int i = 1; i < 5; ++i) p.addLayer();   // Kick, Bass, Hat, Snare, Rumble (on the Kick)
            for (int i = 0; i < 5; ++i) p.getTrack(i).prepare(sr, prepared);
            juce::AudioBuffer<float> out(2, prepared);
            for (int s = 0; s < 5; ++s)
            {
                expectEquals(p.getTrack(s).heldEngineIndex(), s, "track " + juce::String(s) + " holds the engine its parameter names");
                expectEquals(p.getTrack(s).renderPriority(), s == 4 ? 1 : 0, "only the Rumble renders after its peers");
            }
            juce::int64 frame = 0;
            for (const int n : { 1, 64, 512, 512 })
            {
                for (int s = 0; s < 5; ++s)
                {
                    out.clear();
                    p.getTrack(s).render(callAt(s, frame, n, sr, prepared), out, n);
                    expect(allFinite(out, n), "track " + juce::String(s) + " finite at call size " + juce::String(n));
                }
                frame += n;
            }
            // Frames 577..1088 hold the Kick's step 0 tail (it fired at frame 0 of the first call).
            out.clear();
            p.getTrack(0).render(callAt(0, frame, prepared, sr, prepared), out, prepared);
            expect(rmsOf(out, prepared) > 1.0e-4f, "the kick sounds");
            expect(p.getTrack(0).triggers.load() >= 1, "and its step counted a trigger");
        }

        beginTest("a Rumble processes its source's block through the peer table, and silence for any source that is not a peer");
        {
            TempStorage storage;
            PluginProcessor p;
            for (int i = 1; i < 5; ++i) p.addLayer();
            auto& rumble = p.getTrack(4);
            rumble.prepare(sr, prepared);
            setParam(p.apvts, layerParamId(4, "src"), 0.0f);

            juce::AudioBuffer<float> source(2, prepared), out(2, prepared);
            fillBurst(source, prepared);
            std::array<mu_core::PeerTap, mu_limits::kMaxLayers> peers {};
            peers[0] = { { source.getReadPointer(0), source.getReadPointer(1) }, 1, 0 };

            float peak = 0.0f;
            for (int b = 0; b < 8; ++b)
            {
                out.clear();
                rumble.render(callAt(4, b * prepared, prepared, sr, prepared, peers.data()), out, prepared);
                peak = juce::jmax(peak, rmsOf(out, prepared));
            }
            expect(peak > 1.0e-4f, "the Rumble sounds from its source peer");

            // A source the driver did not publish (not live, a priority-1 track, a removed one): silence in, and the
            // chain is skipped once the tail is spent.
            for (const int src : { 7, 4, 1 })
            {
                setParam(p.apvts, layerParamId(4, "src"), (float) src);
                float late = 1.0f;
                for (int b = 0; b < (int) (10.0 * sr / prepared); ++b)
                {
                    out.clear();
                    rumble.render(callAt(4, (8 + b) * prepared, prepared, sr, prepared, peers.data()), out, prepared);
                    late = rmsOf(out, prepared);
                }
                expectEquals(late, 0.0f, "no output once the tail has gone, source " + juce::String(src));
                setParam(p.apvts, layerParamId(4, "src"), 0.0f);
                for (int b = 0; b < 4; ++b)
                {
                    out.clear();
                    rumble.render(callAt(4, b * prepared, prepared, sr, prepared, peers.data()), out, prepared);
                }
                expect(rmsOf(out, prepared) > 0.0f, "and it sounds again on a published source");
            }
        }

        beginTest("an engine change builds the new engine outside render; render itself allocates and frees nothing");
        {
            TempStorage storage;
            PluginProcessor p;
            auto& t = p.getTrack(0);
            t.prepare(sr, prepared);
            juce::AudioBuffer<float> out(2, prepared);
            for (const int e : { 1, 2, 3, 4, 0 })
            {
                setParam(p.apvts, layerParamId(0, "eng"), (float) e);   // the message thread (this one) builds it now
                expectEquals(t.heldEngineIndex(), e, "the track holds the newly named engine at once");
                expectEquals((int) t.typeId(), (int) mu_core::LayerTypeId::Kick + e);
                out.clear();
                mu_alloc::Armed armed;
                t.render(callAt(0, 0, prepared, sr, prepared), out, prepared);
                armed.stop();
                expectEquals(armed.allocs(), 0, "no allocation in render, engine " + juce::String(e));
                expectEquals(armed.frees(), 0, "no free in render, engine " + juce::String(e));
            }
        }

        beginTest("RumbleEngine: reset clears the reverb tail; a spent silent feed is skipped; a returning feed plays");
        {
            RumbleEngine r;
            r.prepare(sr, prepared);
            // Drive 0, no delay taps, a fully wet reverb: the output is the reverb alone.
            r.setParams(120.0, 0.0f, 0.0f, 0.0f, 0.0f, 0.8f, 1.0f, 18000.0f, 20000.0f, 0.0f);

            juce::AudioBuffer<float> in(2, prepared), silent(2, prepared), out(2, prepared);
            fillBurst(in, prepared);
            silent.clear();

            for (int b = 0; b < 3; ++b) { out.clear(); r.render(in, out, prepared); }
            out.clear(); r.render(silent, out, prepared);
            expect(rmsOf(out, prepared) > 1.0e-6f, "a reverb tail follows the feed");
            out.clear(); r.render(silent, out, prepared);

            r.reset();
            out.clear(); r.render(silent, out, prepared);
            expectEquals(rmsOf(out, prepared), 0.0f, "nothing resurfaces after a reset");

            for (int b = 0; b < 3; ++b) { out.clear(); r.render(in, out, prepared); }
            float quiet = 1.0f;
            for (int b = 0; b < (int) (10.0 * sr / prepared); ++b)
            {
                out.clear(); r.render(silent, out, prepared, true);
                quiet = rmsOf(out, prepared);
            }
            expectEquals(quiet, 0.0f, "a silent feed ends in silence");

            out.clear(); r.render(in, out, prepared);
            expect(rmsOf(out, prepared) > 1.0e-6f, "a real feed plays again after the idle spell");
        }
    }
};

static TrackRenderTest trackRenderTest;
