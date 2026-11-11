// Step parameter locks: the generic StepLockSet and LockSeed (mu-core), a step row's locks and their saved form, and a Track
// applying a step's lock from the block the step fires in until the next step that fires.

#include "Plugin/PluginProcessor.h"
#include "Tests/ProcessorSeamHelpers.h"

namespace
{
    using namespace mu_on;

    mu_core::RenderContext callAt(int slot, juce::int64 frame, int n, double sr, int prepared, bool playing = true)
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
        c.playing = playing ? 1 : 0;
        return c;
    }
}

class StepLockTest : public juce::UnitTest
{
public:
    StepLockTest() : juce::UnitTest("mu-on step locks (p-locks)", "Sequencer") {}

    void runTest() override
    {
        beginTest("StepLockSet: set, replace, remove, full, empty");
        {
            mu_core::StepLockSet set;
            expect(set.empty());
            float v = -1.0f;
            expect(set.set(5, 0.25f));
            expect(set.find(5, v) && std::abs(v - 0.25f) < 1.0e-4f);
            expect(set.set(5, 0.75f) && set.count() == 1, "the same key replaces");
            expect(set.find(5, v) && std::abs(v - 0.75f) < 1.0e-4f);
            for (int k = 1; k < mu_core::kMaxStepLocks; ++k) expect(set.set((uint16_t) (100 + k), 0.5f));
            expectEquals(set.count(), mu_core::kMaxStepLocks);
            expect(! set.set(999, 0.5f), "a new key is refused when full");
            expect(set.set(5, 0.1f), "an existing key still changes when full");
            set.remove(5);
            expect(! set.has(5) && set.count() == mu_core::kMaxStepLocks - 1);
            set.clear();
            expect(set.empty());
            expect(set.set(0, 2.0f) && set.find(0, v) && v == 1.0f, "values clamp to 0..1; key 0 is a valid key");
        }

        beginTest("LockSeed picks the keys of one group below the count");
        {
            mu_core::StepLockSet set;
            set.set((uint16_t) StepRow::lockKey(1, 2), 0.4f);
            set.set((uint16_t) StepRow::lockKey(0, 3), 0.9f);     // another engine
            set.set((uint16_t) StepRow::lockKey(1, 40), 0.9f);    // beyond the table
            mu_core::LockSeed seed;
            seed.load(set, 1, 10);
            expectEquals((int) seed.mask, 1 << 2);
            expect(std::abs(seed.value01[2] - 0.4f) < 1.0e-4f);
        }

        beginTest("a row's locks round-trip through its saved form, sparsely, and an old state loads with none");
        {
            StepRow row;
            expect(! row.serialise(0).hasProperty("lock"), "no locks: nothing saved");
            row.setLock(3, 0, 0, 0.5f);
            row.setLock(3, 0, 4, 1.0f);
            row.setLock(9, 2, 1, 0.0f);
            row.setLockModulated(9, true);
            expectEquals(row.lockCount(3), 2);
            StepRow loaded;
            loaded.deserialise(row.serialise(0));
            float v = -1.0f;
            expect(loaded.findLock(3, 0, 0, v) && std::abs(v - 0.5f) < 1.0e-4f);
            expect(loaded.findLock(3, 0, 4, v) && v == 1.0f);
            expect(loaded.findLock(9, 2, 1, v) && v == 0.0f);
            expect(loaded.lockModulated(9) && ! loaded.lockModulated(3));
            expectEquals(loaded.lockCount(0), 0);
            loaded.deserialise(juce::ValueTree("Track"));
            expectEquals(loaded.lockCount(3), 0, "an absent lock list clears the row's locks");
            row.clearLocks(3);
            expectEquals(row.lockCount(3), 0);
            expect(! row.setLock(-1, 0, 0, 0.5f), "an invalid step takes none");
        }

        beginTest("a Track applies a step's lock from the block it fires in, holds it to the next step that fires, and stop ends it");
        {
            mu_seam_test::TempStorage storage;
            PluginProcessor p;
            auto& track = p.getTrack(0);   // a Kick with the default four-on-the-floor row: steps 0, 4, 8, 12
            constexpr double sr = 48000.0;
            constexpr int prepared = 512;
            track.prepare(sr, prepared);
            const auto tuneId = engineParamId(0, EngineType::Kick, "tune");
            const auto range = p.apvts.getParameterRange(tuneId);
            const float own = *p.apvts.getRawParameterValue(tuneId);
            const float locked = range.convertFrom0to1(0.25f);
            expect(std::abs(own - locked) > 1.0e-3f, "the lock differs from the knob");
            track.row.setLock(4, toIndex(EngineType::Kick), 0, 0.25f);   // dest 0 of the Kick table is Tune

            juce::AudioBuffer<float> out(2, prepared);
            auto live = [&] { return track.getModLive(EngineType::Kick, 0); };
            auto render = [&](juce::int64 frame, bool playing = true) { out.clear(); track.render(callAt(0, frame, prepared, sr, prepared, playing), out, prepared); };

            const juce::int64 step4 = 24000, step8 = 48000;   // 6000 frames per step at 120 BPM
            juce::int64 frame = 0;
            for (; frame + prepared <= step4 - prepared; frame += prepared) { render(frame); expectWithinAbsoluteError(live(), own, 1.0e-3f); }
            for (; frame <= step4 + prepared; frame += prepared) render(frame);   // the block holding step 4 and one after
            expectWithinAbsoluteError(live(), locked, 1.0e-2f * std::abs(locked) + 1.0e-3f);
            for (; frame < step8 - prepared; frame += prepared) render(frame);
            expectWithinAbsoluteError(live(), locked, 1.0e-2f * std::abs(locked) + 1.0e-3f);   // held until the next step that fires
            for (; frame <= step8 + prepared; frame += prepared) render(frame);
            expectWithinAbsoluteError(live(), own, 1.0e-3f);   // step 8 has no lock: the track's own value is back

            // Back to step 4 of the next pass, then stop: the track's own values return.
            for (; frame <= 96000 + step4 + prepared; frame += prepared) render(frame);
            expectWithinAbsoluteError(live(), locked, 1.0e-2f * std::abs(locked) + 1.0e-3f);
            render(frame, false);
            expectWithinAbsoluteError(live(), own, 1.0e-3f);
        }
    }
};

static StepLockTest stepLockTest;
