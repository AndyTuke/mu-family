// Track::render on the audio path, outside the processor's block driver: each engine at every call size, a
// Rumble reading its source through RenderContext::peer (and silence for an absent, priority-1 or removed
// source), the engine swap a parameter change makes (built on the calling thread, never inside render), and
// the RumbleEngine's own tail handling (cleared by reset, skipped once a silent feed has let it die away).
// Formerly GrooveVoicesTests: the per-slot engine owner is gone, each Track holds its one engine.

#include "Plugin/PluginProcessor.h"
#include "Tests/ProcessorSeamHelpers.h"
#include "Tests/AllocationProbe.h"   // mu-core: heap activity inside a scope
#include "Tests/GoldenRender.h"      // mu-core: MockHost

#include <thread>

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
                for (int e = 0; e < kNumEngineTypes; ++e)
                    expect(p.getTrack(s).holdsEngine(engineFromIndex(e)), "track " + juce::String(s) + " holds every engine, prepared");
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

        beginTest("an engine change switches in the call that sees it; render itself allocates and frees nothing");
        {
            TempStorage storage;
            PluginProcessor p;
            auto& t = p.getTrack(0);
            t.prepare(sr, prepared);
            juce::AudioBuffer<float> out(2, prepared);
            for (const int e : { 1, 2, 3, 4, 0 })
            {
                setParam(p.apvts, layerParamId(0, "eng"), (float) e);
                expect(! t.syncEngine(), "an unlocked track already holds every engine: nothing to build");
                expectEquals((int) t.typeId(), (int) mu_core::LayerTypeId::Kick + e);
                out.clear();
                mu_alloc::Armed armed;
                t.render(callAt(0, 0, prepared, sr, prepared), out, prepared);
                armed.stop();
                expectEquals(armed.allocs(), 0, "no allocation in render, engine " + juce::String(e));
                expectEquals(armed.frees(), 0, "no free in render, engine " + juce::String(e));
            }
        }

        beginTest("a locked track holds only its named engine; a message-thread change builds and swaps it, any other waits silent");
        {
            TempStorage storage;
            PluginProcessor p;
            auto& t = p.getTrack(0);
            t.engineLocked = true;
            expect(t.syncEngine(), "locking frees the other four");
            for (int e = 0; e < kNumEngineTypes; ++e)
                expect(t.holdsEngine(engineFromIndex(e)) == (e == 0), "only the Kick is held, engine " + juce::String(e));
            setParam(p.apvts, layerParamId(0, "eng"), 1.0f);   // this (the message) thread: built now
            expect(t.holdsEngine(EngineType::Bass) && ! t.holdsEngine(EngineType::Kick), "the Bass replaced the Kick");
            t.prepare(sr, prepared);
            juce::AudioBuffer<float> out(2, prepared);
            std::thread host([&p] { setParam(p.apvts, layerParamId(0, "eng"), 2.0f); });   // not the message thread
            host.join();
            expect(! t.holdsEngine(EngineType::Hat), "a change from another thread is not built there");
            out.clear();
            t.render(callAt(0, 0, prepared, sr, prepared), out, prepared);
            expectEquals(out.getMagnitude(0, prepared), 0.0f, "the locked track is silent until the message thread builds it");
            p.flushPendingAsyncUpdates();
            expect(t.holdsEngine(EngineType::Hat), "the deferred work built it");
        }

        beginTest("host automation of the engine from another thread at a step: the step sounds in that block on the new engine, "
                  "and an offline render (no deferred work) equals the realtime one");
        {
            // Three processors, one Kick track on four on the floor: the engine set to Bass from the message thread
            // (a), from another thread with no deferred work ever run (b, an offline render), and from another thread
            // with the deferred work run after every block (c, realtime with the message thread's poll).
            TempStorage storage;
            struct Run { std::unique_ptr<PluginProcessor> p; mu_golden::MockHost host; juce::AudioBuffer<float> all; };
            std::array<Run, 3> runs;
            constexpr int kBlocks = 96, kN = 512, kSwitchBlock = 46;   // block 46 holds frame 24000: step 4 (beat 1)
            for (auto& r : runs)
            {
                r.p = std::make_unique<PluginProcessor>();
                r.host.bpm = 120.0;
                r.p->setPlayHead(&r.host);
                r.p->setRateAndBufferSizeDetails(sr, kN);
                r.p->prepareToPlay(sr, kN);
                r.all.setSize(2, kBlocks * kN);
            }
            for (int b = 0; b < kBlocks; ++b)
                for (size_t k = 0; k < runs.size(); ++k)
                {
                    auto& r = runs[k];
                    if (b == kSwitchBlock || b == kSwitchBlock + 20)
                    {
                        const float engine = b == kSwitchBlock ? 1.0f : 3.0f;   // Bass, then Snare
                        if (k == 0) setParam(r.p->apvts, layerParamId(0, "eng"), engine);
                        else { std::thread automation([&r, engine] { setParam(r.p->apvts, layerParamId(0, "eng"), engine); }); automation.join(); }
                    }
                    juce::AudioBuffer<float> view(r.all.getArrayOfWritePointers(), 2, b * kN, kN);
                    juce::MidiBuffer midi;
                    r.host.timeInSamples = (juce::int64) b * kN;
                    r.p->processBlock(view, midi);
                    r.host.ppq += (double) kN * 120.0 / (60.0 * sr);
                    if (k == 2) r.p->flushPendingAsyncUpdates();
                }
            bool sameB = true, sameC = true;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < kBlocks * kN; ++i)
                {
                    sameB = sameB && runs[1].all.getSample(c, i) == runs[0].all.getSample(c, i);
                    sameC = sameC && runs[2].all.getSample(c, i) == runs[0].all.getSample(c, i);
                }
            expect(sameB, "automation from another thread, never pumped (offline), equals the message-thread change");
            expect(sameC, "and so does the realtime render that runs the deferred work");
            const int stepFrame = 24000 - kSwitchBlock * kN;
            expect(runs[1].all.getMagnitude(0, kSwitchBlock * kN + stepFrame, kN - stepFrame) > 0.01f, "the step in the switch block sounds");
            expectEquals(runs[1].p->getTrack(0).triggers.load(), runs[0].p->getTrack(0).triggers.load(), "no step was dropped");
            for (auto& r : runs) { r.p->setPlayHead(nullptr); r.p->releaseResources(); }
        }

        beginTest("C12 hold ends when the track parks: unparked with its matrix lock held, it takes the new unmodulated values");
        {
            TempStorage storage;
            PluginProcessor p;
            p.selectEngine(0, EngineType::Bass);
            auto& t = p.getTrack(0);
            t.prepare(sr, prepared);
            auto& cs = t.controlSequences[0];
            cs.mode = ControlSequence::Mode::Stepped;
            cs.polarity = ControlSequence::Polarity::Unipolar;
            cs.stepValues = { 100.0f };
            const auto id = engineParamId(0, EngineType::Bass, "cut");
            const auto range = p.apvts.getParameterRange(id);
            setParam(p.apvts, id, range.convertFrom0to1(0.5f));
            ModulationAssignment a;
            a.id = "m"; a.sourceId = "cs0_output"; a.destinationId = "b.cut.prop"; a.depth = 40.0f;
            expect(t.modulationMatrix.addAssignment(a));
            juce::AudioBuffer<float> out(2, prepared);
            out.clear();
            t.render(callAt(0, 0, prepared, sr, prepared), out, prepared);
            expect(range.convertTo0to1(t.getModLive(EngineType::Bass, 2)) > 0.7f, "modulated while live");

            t.onParkedBlock(callAt(0, prepared, prepared, sr, prepared));
            setParam(p.apvts, id, range.convertFrom0to1(0.3f));                 // changed while parked
            t.modLock.store(true, std::memory_order_release);                     // the matrix is being edited
            out.clear();
            t.render(callAt(0, 2 * prepared, prepared, sr, prepared), out, prepared);
            t.modLock.store(false, std::memory_order_release);
            expectWithinAbsoluteError(range.convertTo0to1(t.getModLive(EngineType::Bass, 2)), 0.3f, 0.01f,
                                      "not the value held from before it parked");
        }

        beginTest("a stop and restart hidden in blocks an edit silenced resets a sustaining Bass (stoppedUnrendered)");
        {
            TempStorage storage;
            PluginProcessor p;
            p.selectEngine(0, EngineType::Bass);
            for (int s = 1; s < 16; ++s) p.getTrack(0).row.setOn(s, false);
            p.getTrack(0).row.setOn(0, true);
            setParam(p.apvts, engineParamId(0, EngineType::Bass, "sus"), 1.0f);
            mu_golden::MockHost host;
            host.bpm = 120.0;
            p.setPlayHead(&host);
            p.setRateAndBufferSizeDetails(sr, prepared);
            p.prepareToPlay(sr, prepared);
            juce::AudioBuffer<float> buf(2, prepared);
            auto block = [&]
            {
                buf.clear();
                juce::MidiBuffer midi;
                p.processBlock(buf, midi);
                if (host.playing) host.ppq += (double) prepared * 120.0 / (60.0 * sr);
                return buf.getMagnitude(0, prepared);
            };
            float playing = 0.0f;
            for (int b = 0; b < 4; ++b) playing = block();
            expect(playing > 0.1f, "the held bass sounds");
            p.layerEditInFlight.store(true);
            host.playing = false; block();
            host.playing = true;  block();
            p.layerEditInFlight.store(false);
            float after = 0.0f;
            for (int b = 0; b < 4; ++b) after = juce::jmax(after, block());
            expectEquals(after, 0.0f, "the stop no rendered block saw still silenced the bass");
            p.setPlayHead(nullptr);
            p.releaseResources();
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
