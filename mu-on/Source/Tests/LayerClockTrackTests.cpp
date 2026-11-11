// Verification of the 2026-11-11 work through a real processor and Track: the layer clock (Speed, Layer Length, the sequence's cap, Play Mode
// as a parameter and as a modulation target), its saved state, and the step parameter locks with a real modulator (Mod On and Off) and
// under Backwards. The clock arithmetic itself is in LayerClockTests.cpp; these tests go through Track::render.

#include "Plugin/PluginProcessor.h"
#include "Tests/ProcessorSeamHelpers.h"
#include "Sequencer/StepClock.h"

namespace
{
    using namespace mu_on;

    constexpr double kSr = 48000.0;
    constexpr int kBlock = 512;
    constexpr double kBeatsPerFrame = 2.0 / kSr;   // 120 BPM

    mu_core::RenderContext callAt(juce::int64 frame, bool first = false)
    {
        auto c = mu_core::makeRenderContext();
        c.sampleRate = kSr;
        c.blockSize = kBlock;
        c.preparedBlockSize = kBlock;
        c.slot = 0;
        c.bpm = 120.0;
        c.beatsPerSample = kBeatsPerFrame;
        c.startBeat = (double) frame * kBeatsPerFrame;
        c.callBeats = (double) kBlock * kBeatsPerFrame;
        c.playing = 1;
        c.justStarted = first ? 1 : 0;
        return c;
    }

    void setOnly(StepRow& row, std::initializer_list<int> steps)
    {
        for (int s = 0; s < StepRow::kMaxSteps; ++s) row.setOn(s, false);
        for (int s : steps) row.setOn(s, true);
    }

    // Renders contiguous blocks from `from` for `frames` frames; returns the beat (block start) of each block in which the track triggered.
    std::vector<double> firings(Track& track, juce::int64 from, juce::int64 frames)
    {
        std::vector<double> at;
        juce::AudioBuffer<float> out(2, kBlock);
        for (juce::int64 f = from; f < from + frames; f += kBlock)
        {
            const int before = track.triggers.load();
            out.clear();
            track.render(callAt(f, f == 0), out, kBlock);
            if (track.triggers.load() > before) at.push_back((double) f * kBeatsPerFrame);
        }
        return at;
    }
}

class LayerClockTrackTest : public juce::UnitTest
{
public:
    LayerClockTrackTest() : juce::UnitTest("mu-on layer clock and p-locks through a Track", "Sequencer") {}

    void runTest() override
    {
        mu_seam_test::TempStorage storage;

        beginTest("Speed: 1/8 plays half as many steps in a bar as 1/16");
        {
            PluginProcessor p;
            auto& track = p.getTrack(0);
            track.prepare(kSr, kBlock);
            setOnly(track.row, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 });
            const auto speedId = layerParamId(0, "speed");
            mu_seam_test::setParam(p.apvts, speedId, (float) step_repeats::kDefaultRate);
            const auto sixteenths = firings(track, 0, 96000 - kBlock);   // a bar, less the block that would hold the next bar's first step
            mu_seam_test::setParam(p.apvts, speedId, 1.0f);              // 1/8
            const auto eighths = firings(track, 0, 96000 - kBlock);
            expectEquals((int) sixteenths.size(), 16, "sixteenths: a step every block-ish, one firing block per step");
            expectEquals((int) eighths.size(), 8, "eighths: half as many");
            expectWithinAbsoluteError(track.stepBeatsNow(), 0.5, 1.0e-9);
        }

        beginTest("Layer Length: a 12-step layer's last step comes round after 12 steps, a 16-step layer's after 16");
        {
            PluginProcessor p;
            auto& track = p.getTrack(0);
            track.prepare(kSr, kBlock);
            mu_seam_test::setParam(p.apvts, kSeqBarsParamId, 2.0f);       // a two-bar sequence: room for either length
            setOnly(track.row, { 11 });
            track.row.setNumSteps(12);
            const auto twelve = firings(track, 0, 8 * 24000);             // 8 beats
            track.row.setNumSteps(16);
            const auto sixteen = firings(track, 0, 8 * 24000);
            expectEquals((int) twelve.size(), 2);
            expectEquals((int) sixteen.size(), 2);
            if (twelve.size() == 2 && sixteen.size() == 2)
            {
                expectWithinAbsoluteError(twelve[0], 2.75, 0.03);   // step 11 of 12: 2.75 beats in, then every 3 beats
                expectWithinAbsoluteError(twelve[1], 5.75, 0.03);
                expectWithinAbsoluteError(sixteen[0], 2.75, 0.03);
                expectWithinAbsoluteError(sixteen[1], 6.75, 0.03);  // every 4 beats
            }
        }

        beginTest("the sequence's length caps every layer, and a longer sequence gives the layer's own length back");
        {
            PluginProcessor p;
            auto& track = p.getTrack(0);
            track.prepare(kSr, kBlock);
            track.row.setNumSteps(48);
            firings(track, 0, kBlock);
            expectEquals(track.row.numSteps(), 16, "a one-bar sequence holds a 48-step layer to 16");
            expectEquals(track.row.ownNumSteps(), 48, "and keeps its own length");
            mu_seam_test::setParam(p.apvts, kSeqBarsParamId, 3.0f);
            firings(track, 0, kBlock);
            expectEquals(track.row.numSteps(), 48);
            mu_seam_test::setParam(p.apvts, kSeqBarsParamId, 2.0f);
            firings(track, 0, kBlock);
            expectEquals(track.row.numSteps(), 32);
        }

        beginTest("Play Mode Backwards plays step 15 first; Forwards does not play it until the end of the bar");
        {
            PluginProcessor p;
            auto& track = p.getTrack(0);
            track.prepare(kSr, kBlock);
            setOnly(track.row, { 15 });
            const auto modeId = layerParamId(0, "mode");
            mu_seam_test::setParam(p.apvts, modeId, 0.0f);
            const auto forwards = firings(track, 0, 24000);               // the first beat
            mu_seam_test::setParam(p.apvts, modeId, 1.0f);
            const auto backwards = firings(track, 0, 24000);
            expectEquals((int) forwards.size(), 0, "forwards: step 15 is at beat 3.75");
            expectEquals((int) backwards.size(), 1, "backwards: step 15 is the first");
            if (! backwards.empty()) expectWithinAbsoluteError(backwards[0], 0.0, 0.03);
        }

        beginTest("Random visits the whole layer: over many slots every step of a 12-step layer is picked, about evenly");
        {
            ClockSetup c;
            c.mode = ClockSetup::kRandom;
            c.seed = 0xBEEF;
            int counts[12] {};
            for (int slot = 0; slot < 12 * 400; ++slot) ++counts[StepClock::positionAt(slot * 0.25, 12, c).step];
            for (int s = 0; s < 12; ++s) expect(counts[s] > 400 / 2 && counts[s] < 400 * 3 / 2, "step " + juce::String(s) + " picked " + juce::String(counts[s]) + " times");
        }

        beginTest("Play Mode is a modulation target: a modulator moves the resolved mode off the parameter");
        {
            PluginProcessor p;
            auto& track = p.getTrack(0);
            track.prepare(kSr, kBlock);
            mu_seam_test::setParam(p.apvts, layerParamId(0, "mode"), 0.0f);
            auto& cs = track.controlSequences[0];
            cs.mode = ControlSequence::Mode::Stepped;
            cs.polarity = ControlSequence::Polarity::Unipolar;
            cs.stepValues.assign(16, 100.0f);   // every step full, so the modulator holds across the bar
            firings(track, 0, kBlock);
            expectEquals(track.clockSetupNow().mode, 0, "no modulator: the parameter's Forwards");

            ModulationAssignment a;
            a.id = "mode"; a.sourceId = "cs0_output"; a.destinationId = "perf.mode"; a.depth = 100.0f;
            expect(track.modulationMatrix.addAssignment(a), "the Play Mode destination is in the Kick's table");
            firings(track, 0, kBlock);
            expect(track.clockSetupNow().mode > 0, "a full-depth modulator moves Forwards on to Backwards or Random");

            track.modulationMatrix.removeAssignment("mode");
            firings(track, 0, kBlock);
            expectEquals(track.clockSetupNow().mode, 0, "and it falls back to the parameter when the assignment goes");
        }

        beginTest("the layer's clock settings and the sequence's survive a save and load");
        {
            PluginProcessor p;
            mu_seam_test::setParam(p.apvts, kSeqBarsParamId, 4.0f);
            mu_seam_test::setParam(p.apvts, kSeqResetParamId, 8.0f);
            mu_seam_test::setParam(p.apvts, layerParamId(0, "speed"), 2.0f);
            mu_seam_test::setParam(p.apvts, layerParamId(0, "mode"), 1.0f);
            p.getTrack(0).row.setNumSteps(12);
            juce::MemoryBlock state;
            p.getStateInformation(state);

            PluginProcessor q;
            q.setStateInformation(state.getData(), (int) state.getSize());
            auto value = [&](const juce::String& id) { return q.apvts.getRawParameterValue(id)->load(); };
            expectEquals(value(kSeqBarsParamId), 4.0f);
            expectEquals(value(kSeqResetParamId), 8.0f);
            expectEquals(value(layerParamId(0, "speed")), 2.0f);
            expectEquals(value(layerParamId(0, "mode")), 1.0f);
            expectEquals(q.getTrack(0).row.ownNumSteps(), 12, "the layer's own length is in the saved row");
        }

        beginTest("a layer longer than a shorter sequence round-trips whole: its own length is saved, not the capped one");
        {
            PluginProcessor p;
            p.getTrack(0).row.setNumSteps(48);
            p.getTrack(0).row.setStepCap(16);
            const auto saved = p.getTrack(0).row.serialise(0);
            StepRow loaded;
            loaded.deserialise(saved);
            expectEquals(loaded.ownNumSteps(), 48);
        }

        // ── P-locks with a real modulator ─────────────────────────────────────────────────────────────
        // Kick Tune (dest 0 of the Kick table, `k.tune.prop`) with a constant +100 modulator at 40 % depth; step 4 carries a lock of 0.25.
        auto lockRig = [&](bool modulate, PluginProcessor& p, juce::AudioBuffer<float>& out, double& locked, double& own)
        {
            auto& track = p.getTrack(0);
            track.prepare(kSr, kBlock);
            const auto tuneId = engineParamId(0, EngineType::Kick, "tune");
            const auto range = p.apvts.getParameterRange(tuneId);
            mu_seam_test::setParam(p.apvts, tuneId, range.convertFrom0to1(0.2f));
            auto& cs = track.controlSequences[0];
            cs.mode = ControlSequence::Mode::Stepped;
            cs.polarity = ControlSequence::Polarity::Unipolar;
            cs.stepValues.assign(16, 100.0f);   // every step full, so the modulator holds across the bar
            ModulationAssignment a;
            a.id = "tune"; a.sourceId = "cs0_output"; a.destinationId = "k.tune.prop"; a.depth = 40.0f;
            track.modulationMatrix.addAssignment(a);
            track.row.setLock(4, toIndex(EngineType::Kick), 0, 0.25f);
            track.row.setLockModulated(4, modulate);
            locked = range.convertFrom0to1(0.25f);
            own = range.convertFrom0to1(0.2f + 0.4f);   // the knob plus the modulator
            out.setSize(2, kBlock);
        };
        auto liveAfterStep4 = [&](PluginProcessor& p, juce::AudioBuffer<float>& out)
        {
            auto& track = p.getTrack(0);
            for (juce::int64 f = 0; f <= 24000 + 2 * kBlock; f += kBlock)
            {
                out.clear(); track.render(callAt(f, f == 0), out, kBlock);
            }
            return (double) track.getModLive(EngineType::Kick, 0);
        };

        beginTest("a lock with Mod Off pins the knob to the lock while a modulator runs");
        {
            PluginProcessor p;
            juce::AudioBuffer<float> out;
            double locked = 0, own = 0;
            lockRig(false, p, out, locked, own);
            const double live = liveAfterStep4(p, out);
            expectWithinAbsoluteError(live, locked, 0.02 * std::abs(locked));
        }

        beginTest("a lock with Mod On starts the modulator from the lock: higher than the lock, and not at the unlocked value");
        {
            PluginProcessor p;
            juce::AudioBuffer<float> out;
            double locked = 0, own = 0;
            lockRig(true, p, out, locked, own);
            const double live = liveAfterStep4(p, out);
            const auto range = p.apvts.getParameterRange(engineParamId(0, EngineType::Kick, "tune"));
            expect(live > locked + 1.0e-3, "the modulator adds to the lock");
            expectWithinAbsoluteError((double) range.convertTo0to1((float) live), 0.25 + 0.4, 0.03);
        }

        beginTest("a lock applies to the step Backwards actually plays: step 15 first, so the lock on step 15 holds from the first block");
        {
            PluginProcessor p;
            auto& track = p.getTrack(0);
            track.prepare(kSr, kBlock);
            const auto tuneId = engineParamId(0, EngineType::Kick, "tune");
            const auto range = p.apvts.getParameterRange(tuneId);
            mu_seam_test::setParam(p.apvts, tuneId, range.convertFrom0to1(0.2f));
            setOnly(track.row, { 15 });
            track.row.setLock(15, toIndex(EngineType::Kick), 0, 0.8f);
            mu_seam_test::setParam(p.apvts, layerParamId(0, "mode"), 1.0f);   // backwards
            juce::AudioBuffer<float> out(2, kBlock);
            track.render(callAt(0, true), out, kBlock);
            expectWithinAbsoluteError((double) range.convertTo0to1(track.getModLive(EngineType::Kick, 0)), 0.8, 0.02);

            mu_seam_test::setParam(p.apvts, layerParamId(0, "mode"), 0.0f);   // forwards: step 15 is at beat 3.75, nothing locked yet
            PluginProcessor q;
            auto& t2 = q.getTrack(0);
            t2.prepare(kSr, kBlock);
            mu_seam_test::setParam(q.apvts, tuneId, range.convertFrom0to1(0.2f));
            setOnly(t2.row, { 15 });
            t2.row.setLock(15, toIndex(EngineType::Kick), 0, 0.8f);
            t2.render(callAt(0, true), out, kBlock);
            expectWithinAbsoluteError((double) range.convertTo0to1(t2.getModLive(EngineType::Kick, 0)), 0.2, 0.02);
        }
    }
};

static LayerClockTrackTest layerClockTrackTest;
