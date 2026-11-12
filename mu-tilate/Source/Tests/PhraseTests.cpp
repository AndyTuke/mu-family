// The timeline model and the Phrase scheduler: edits snap and keep order, a placed block sounds at its beat whatever
// the host block size, a new block or a block's own length cuts the slice before it, a transport jump re-anchors,
// and the tempo modes change the slice's length as the design says.

#include "AllocationProbe.h"   // mu-core/Tests (its operators are defined in StretchSliceTests.cpp)

#include "Sequencer/PhrasePlayback.h"

#include <juce_core/juce_core.h>

#include <cmath>

using namespace mu_tilate;

namespace
{
constexpr double kSr = 48000.0;

// A 2 s loop of four 0.5 s slices, slice k holding the constant (k + 1) / 10: one beat each at 120 BPM.
struct Rig
{
    juce::AudioBuffer<float> audio { 1, 96000 };
    SliceList slices { 96000 };
    Timeline timeline { 4.0 };
    PhraseLoop loop;
    PhraseSettings settings;
    PhrasePlayback playback;
    bool hitsStartWithSliceSettings = true;   // a hit placed before the slice was set up still takes that slice's settings, as a hit placed after would

    Rig()
    {
        for (int k = 0; k < 4; ++k)
            for (int i = 0; i < 24000; ++i) audio.setSample(0, k * 24000 + i, (float) (k + 1) / 10.0f);
        slices.divideEvenly(4);
        loop.audio = &audio; loop.fileSampleRate = kSr; loop.slices = &slices; loop.timeline = &timeline;
        settings.fadeInMs = 0.0f; settings.fadeOutMs = 0.0f;
        settings.mode = TempoMode::Off;   // the tests that want the tempo match switch it on
        playback.prepare(kSr, 512);
    }

    // Renders `total` samples from `startBeat` at `bpm`, in blocks of `block`, and returns the mono output.
    juce::AudioBuffer<float> run(int total, int block, double startBeat = 0.0, double bpm = 120.0, bool playing = true)
    {
        juce::AudioBuffer<float> out(1, total);
        out.clear();
        runInto(out, block, startBeat, bpm, playing);
        return out;
    }

    // The same into a buffer the caller owns (so a heap probe around it sees only the scheduler).
    void runInto(juce::AudioBuffer<float>& out, int block, double startBeat = 0.0, double bpm = 120.0, bool playing = true)
    {
        const int total = out.getNumSamples();
        if (hitsStartWithSliceSettings)
            for (int i = 0; i < timeline.size(); ++i) timeline.setSettings(i, slices.settings(timeline.blocks()[(size_t) i].slice));
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min(block, total - pos);
            PhraseClock c;
            c.playing = playing; c.bpm = bpm; c.sampleRate = kSr;
            c.beatsPerSample = bpm / 60.0 / kSr;
            c.startBeat = startBeat + (double) pos * c.beatsPerSample;
            juce::AudioBuffer<float> view(out.getArrayOfWritePointers(), 1, pos, n);
            playback.render(loop, settings, c, view, n);
        }
    }
};

double rms(const juce::AudioBuffer<float>& b, int from, int count)
{
    double s = 0.0;
    for (int i = from; i < from + count; ++i) s += (double) b.getSample(0, i) * b.getSample(0, i);
    return std::sqrt(s / (double) count);
}
}

struct PhraseTests : public juce::UnitTest
{
    PhraseTests() : juce::UnitTest("Phrase timeline and playback", "mu-tilate") {}

    void runTest() override
    {
        beginTest("timeline: place snaps, keeps start order, replaces on the same start, refuses outside");
        {
            Timeline t(4.0);
            expectEquals(t.place(1, 2.1, 0.25), 0);
            expectEquals(t.place(0, 0.0, 0.25), 0);                     // sorts before
            expectEquals(t.place(2, 2.0, 0.25), 1);                     // same start as slice 1: replaces it
            expectEquals(t.size(), 2);
            expectEquals(t.blocks()[1].slice, 2);
            expectEquals(t.place(0, 4.0, 0.25), -1);
            expectEquals(t.place(0, -1.0, 0.25), -1);
            expectEquals(t.blocks()[1].start, 2.0);
            expectEquals(Timeline::snap(2.13, 0.0), 2.13);              // Shift: no snap
        }

        beginTest("timeline: move, copy, length, remove, shortening the timeline drops blocks beyond it");
        {
            Timeline t(8.0);
            t.place(0, 0.0, 1.0); t.place(1, 4.0, 1.0);
            expectEquals(t.move(0, 5.0, 1.0), 1);                       // now after the other
            expectEquals(t.blocks()[0].slice, 1);
            expectEquals(t.copy(0, 1.0, 1.0), 0);
            expectEquals(t.size(), 3);
            expect(t.setBlockLength(0, 0.4, 0.25));
            expectEquals(t.blocks()[0].length, 0.5);
            expect(t.remove(2));
            t.setLengthBeats(4.0);
            expectEquals(t.size(), 1);                                  // the blocks at 4 and 5 went, the copy at 1 stayed
        }

        beginTest("timeline: blocks follow their slices when one is inserted or removed");
        {
            Timeline t(8.0);
            t.place(0, 0.0, 0.0); t.place(1, 1.0, 0.0); t.place(2, 2.0, 0.0);
            t.sliceInserted(1);                                         // a new slice 1: the old 1 and 2 become 2 and 3
            expectEquals(t.blocks()[0].slice, 0);
            expectEquals(t.blocks()[1].slice, 2);
            expectEquals(t.blocks()[2].slice, 3);
            t.sliceRemoved(2);                                          // slice 2 merged into slice 1
            expectEquals(t.blocks()[1].slice, 1);
            expectEquals(t.blocks()[2].slice, 2);
        }

        beginTest("timeline: original order lays the slices back to back; randomise keeps the layout");
        {
            Timeline t(8.0);
            t.originalOrder({ 1.0, 2.0, 1.0 });
            expectEquals(t.size(), 3);
            expectEquals(t.blocks()[2].start, 3.0);
            juce::Random rng(7);
            t.randomise({ 1.0, 1.0, 1.0, 1.0 }, rng);
            expectEquals(t.size(), 4);
            std::vector<int> seen;
            for (const auto& b : t.blocks()) seen.push_back(b.slice);
            std::sort(seen.begin(), seen.end());
            expect(seen == std::vector<int>({ 0, 1, 2, 3 }));
        }

        beginTest("a block sounds at its beat, and the output does not depend on the host block size");
        {
            Rig a, b;
            a.timeline.place(2, 0.0, 0.0); a.timeline.place(0, 2.0, 0.0);
            b.timeline.place(2, 0.0, 0.0); b.timeline.place(0, 2.0, 0.0);
            auto big = a.run(96000, 512);
            auto odd = b.run(96000, 97);
            expectWithinAbsoluteError(big.getSample(0, 100), 0.3f, 1.0e-5f);             // beat 0: slice 2
            expectWithinAbsoluteError(big.getSample(0, 24000 + 100), 0.0f, 1.0e-6f);     // the slice (one beat, 24000 samples) has ended
            expectWithinAbsoluteError(big.getSample(0, 48000 + 100), 0.1f, 1.0e-5f);     // beat 2: slice 0
            for (int i = 0; i < 96000; ++i)
                if (big.getSample(0, i) != odd.getSample(0, i)) { expect(false, "block size changed the output at " + juce::String(i)); break; }
        }

        beginTest("the next block cuts the slice before it, through the cut fade");
        {
            Rig r;
            r.timeline.place(0, 0.0, 0.0); r.timeline.place(3, 0.25, 0.0);   // the second starts 6000 samples in
            auto out = r.run(24000, 256);
            expectWithinAbsoluteError(out.getSample(0, 5000), 0.1f, 1.0e-5f);
            expectWithinAbsoluteError(out.getSample(0, 6000 + 400), 0.4f, 1.0e-5f);   // the cut fade (3 ms = 144) is over
            // During the fade the two overlap: never louder than their sum.
            float peak = 0.0f;
            for (int i = 5990; i < 6200; ++i) peak = std::max(peak, out.getSample(0, i));
            expectLessOrEqual(peak, 0.5f + 1.0e-5f);
        }

        beginTest("a block's own length stops it early");
        {
            Rig r;
            r.timeline.place(1, 0.0, 0.0);
            r.timeline.setBlockLength(0, 0.25, 0.0);                    // 6000 samples
            auto out = r.run(24000, 128);
            expectWithinAbsoluteError(out.getSample(0, 5000), 0.2f, 1.0e-5f);
            expectEquals(out.getSample(0, 6000 + 400), 0.0f);
        }

        beginTest("a slice's level adds to the layer's, in dB");
        {
            Rig r;
            r.timeline.place(0, 0.0, 0.0);
            SliceSettings own; own.levelDb = -6.0f;
            r.slices.setSettings(0, own);
            auto out = r.run(2000, 256);
            expectWithinAbsoluteError(out.getSample(0, 100), 0.1f * 0.501187f, 1.0e-4f);
        }

        beginTest("a pre-fade swells in before the hit, ending where the slice starts");
        {
            Rig r;
            r.timeline.place(1, 2.0, 0.0);                                // slice 1 (0.2) at beat 2 = sample 48000
            SliceSettings own; own.preFade = true; own.preLengthMs = 100.0f; own.preLevelDb = -12.0f;
            r.slices.setSettings(1, own);
            auto out = r.run(96000, 333);
            expectEquals(out.getSample(0, 40000), 0.0f);                  // before the swell: silence
            expectLessOrEqual(std::abs(out.getSample(0, 43300)), 0.005f); // the swell has just begun
            const float end = out.getSample(0, 47990);                    // the swell is at its full level (0.2 at -12 dB)
            expectGreaterThan(end, 0.045f); expectLessThan(end, 0.052f);
            expectWithinAbsoluteError(out.getSample(0, 48010), 0.2f, 1.0e-5f);   // then the hit itself
        }

        beginTest("a pre-fade mixes over the slice before it, which is not cut");
        {
            Rig r;
            r.timeline.place(0, 1.0, 0.0);                                // slice 0 (0.1) from sample 24000
            r.timeline.place(1, 1.5, 0.0);                                // slice 1 (0.2) at sample 36000
            SliceSettings own; own.preFade = true; own.preLengthMs = 100.0f; own.preLevelDb = -12.0f;
            r.slices.setSettings(1, own);
            auto out = r.run(60000, 128);
            expectWithinAbsoluteError(out.getSample(0, 30000), 0.1f, 1.0e-5f);          // before the swell: the old slice alone
            const float both = out.getSample(0, 35990);                                  // the swell on top of the old slice
            expectGreaterThan(both, 0.145f); expectLessThan(both, 0.152f);
        }

        beginTest("a pre-fade on a block near the start of the timeline sounds at the end of the previous pass");
        {
            Rig r;
            r.timeline.place(1, 0.0, 0.0);
            SliceSettings own; own.preFade = true; own.preLengthMs = 100.0f;
            r.slices.setSettings(1, own);
            auto out = r.run(96000 + 2000, 256);                          // one pass (4 beats = 96000 samples) and a little more
            expectGreaterThan(out.getSample(0, 96000 - 10), 0.04f);       // the end of pass one carries the swell
            expectWithinAbsoluteError(out.getSample(0, 96000 + 50), 0.2f, 1.0e-5f);
        }

        beginTest("a block on the downbeat fires when a call starts a hair before the timeline's end");
        {
            Rig r;
            r.timeline.place(2, 0.0, 0.0);                                // slice 2 (0.3) on the downbeat
            for (double early : { 1.0e-12, 1.0e-10, 3.0e-12 })
            {
                Rig q;
                q.timeline.place(2, 0.0, 0.0);
                auto out = q.run(2000, 256, 4.0 - early);                 // the timeline is 4 beats long
                expectWithinAbsoluteError(out.getSample(0, 100), 0.3f, 1.0e-5f);
            }
        }

        beginTest("a stretched slice's transient hold does not depend on the host block size");
        {
            auto make = [](Rig& r)
            {
                for (int i = 0; i < 24000; ++i) r.audio.setSample(0, i, 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f * (float) i / (float) kSr));
                r.settings.mode = TempoMode::Match; r.settings.stretchMix = 1.0f; r.settings.loopTempo = 120.0; r.settings.holdMs = 10.0f;
                r.timeline.place(0, 0.0, 0.0);
            };
            Rig a, b;
            make(a); make(b);
            auto small = a.run(6000, 64, 0.0, 100.0);
            auto large = b.run(6000, 1024, 0.0, 100.0);
            double worst = 0.0;
            for (int i = 0; i < 6000; ++i) worst = std::max(worst, (double) std::abs(small.getSample(0, i) - large.getSample(0, i)));
            expectLessThan(worst, 0.02);
        }

        beginTest("two hits of one slice are two sounds: each plays with its own settings");
        {
            Rig r;
            r.hitsStartWithSliceSettings = false;
            r.timeline.place(0, 0.0, 0.0);                                // the same slice twice, one beat 0, one at beat 2
            r.timeline.place(0, 2.0, 0.0);
            SliceSettings quiet; quiet.levelDb = -12.0f;
            r.timeline.setSettings(1, quiet);                             // only the second hit is turned down
            auto out = r.run(96000, 256);
            expectWithinAbsoluteError(out.getSample(0, 100), 0.1f, 1.0e-4f);                          // hit one: as the source
            expectWithinAbsoluteError(out.getSample(0, 48100), 0.1f * 0.251189f, 1.0e-4f);            // hit two: -12 dB
        }

        beginTest("the layer's filter applies to every hit and a hit's cutoff offset moves it, in octaves");
        {
            int lp12 = 0;
            for (int i = 0; i < (int) filterChoices().size(); ++i) if (filterChoices()[(size_t) i].name == "LP 12") lp12 = i;
            auto levelWith = [lp12](int type, float offsetOct)
            {
                Rig r;
                r.hitsStartWithSliceSettings = false;
                juce::Random rng(5);
                for (int i = 0; i < 24000; ++i) r.audio.setSample(0, i, 0.5f * (rng.nextFloat() * 2.0f - 1.0f));   // slice 0 is noise
                r.timeline.place(0, 0.0, 0.0);
                SliceSettings own; own.cutoffOct = offsetOct;
                r.timeline.setSettings(0, own);
                r.settings.filterType = type == 1 ? lp12 : 0; r.settings.cutoffHz = 500.0f; r.settings.resonance = 0.0f;
                return rms(r.run(24000, 256), 4000, 16000);
            };
            const double closed = levelWith(1, 0.0f), opened = levelWith(1, 3.0f), off = levelWith(0, 0.0f), offMoved = levelWith(0, 3.0f);
            expectGreaterThan(opened, 1.5 * closed);                      // three octaves up lets much more of the noise through
            expectLessThan(closed, 0.7 * off);                            // the layer's low-pass takes level off the noise
            expectWithinAbsoluteError(off, offMoved, 1.0e-6);             // with the layer's filter off the offsets do nothing
        }

        beginTest("a pre-fade of a slice that plays backwards swells in the end of the slice");
        {
            auto build = [](bool reversed)
            {
                auto r = std::make_unique<Rig>();
                for (int i = 0; i < 24000; ++i) r->audio.setSample(0, 24000 + i, 0.1f + 0.8f * (float) i / 24000.0f);   // slice 1 rises 0.1 -> 0.9
                r->timeline.place(1, 2.0, 0.0);
                SliceSettings own; own.preFade = true; own.preLengthMs = 100.0f; own.preLevelDb = -6.0f; own.reverse = reversed;
                r->slices.setSettings(1, own);
                return r;
            };
            auto fwd = build(false), rev = build(true);
            auto a = fwd->run(96000, 512), b = rev->run(96000, 512);
            expectLessThan(a.getSample(0, 47990), 0.1f);                  // forwards: the swell is the start of the slice (about 0.1)
            expectGreaterThan(b.getSample(0, 47990), 0.3f);               // backwards: the swell is its end (about 0.9)
        }

        beginTest("the pre-fade scan stays cheap with a thousand blocks that all have one (measured, logged)");
        {
            Rig r;
            r.timeline = Timeline(256.0);                                 // 64 bars
            for (int i = 0; i < 1024; ++i) r.timeline.place(i % 4, i * 0.25, 0.0);
            SliceSettings own; own.preFade = true; own.preLengthMs = 50.0f;
            for (int k = 0; k < 4; ++k) r.slices.setSettings(k, own);
            const int block = 64, calls = 1500;
            juce::AudioBuffer<float> out(1, block);
            const auto t0 = juce::Time::getHighResolutionTicks();
            for (int i = 0; i < calls; ++i)
            {
                out.clear();
                PhraseClock c;
                c.playing = true; c.bpm = 120.0; c.sampleRate = kSr; c.beatsPerSample = 120.0 / 60.0 / kSr;
                c.startBeat = (double) i * block * c.beatsPerSample;
                r.playback.render(r.loop, r.settings, c, out, block);
            }
            const double ms = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - t0) * 1000.0;
            const double perCallUs = ms * 1000.0 / calls, budgetUs = block / kSr * 1.0e6;
            logMessage("pre-fade scan, 1024 pre-fade blocks, 64-sample blocks: " + juce::String(perCallUs, 1) + " us per call, the block lasts "
                       + juce::String(budgetUs, 0) + " us (" + juce::String(100.0 * perCallUs / budgetUs, 1) + "% of it), Debug build");
            expectLessThan(perCallUs, budgetUs * 0.5);                    // a loose bound, timing is load-sensitive: the exact check follows

            // The deterministic check: one call looks only at the blocks near it (the longest pre-fade reaches back one second =
            // 2 beats = 8 blocks, plus the blocks of the segment itself), never at the thousand.
            PhraseClock c1;
            c1.playing = true; c1.bpm = 120.0; c1.sampleRate = kSr; c1.beatsPerSample = 120.0 / 60.0 / kSr; c1.startBeat = 100.0;
            out.clear();
            r.playback.render(r.loop, r.settings, c1, out, block);
            expectLessThan(r.playback.preBlocksExamined(), 40);
            expectGreaterThan(r.playback.preBlocksExamined(), 0);
        }

        beginTest("a transport jump re-anchors: the old slice fades and the new position plays");
        {
            Rig r;
            r.timeline.place(0, 0.0, 0.0); r.timeline.place(2, 2.0, 0.0);
            r.run(3000, 128, 0.0);                                      // slice 0 sounding
            auto after = r.run(2000, 128, 2.0);                         // locate to beat 2
            expectWithinAbsoluteError(after.getSample(0, 1000), 0.3f, 1.0e-5f);
            Rig q;
            q.timeline.place(0, 0.0, 0.0);
            q.run(3000, 128, 0.0);
            auto gone = q.run(2000, 128, 1.0);                          // locate to a place with no block
            expectEquals(gone.getSample(0, 1000), 0.0f);
        }

        beginTest("stopped: the sounding slice fades out and nothing starts");
        {
            Rig r;
            r.timeline.place(0, 0.0, 0.0);
            r.run(2000, 128, 0.0);
            auto out = r.run(2000, 128, 0.0, 120.0, false);
            expectEquals(out.getSample(0, 1999), 0.0f);
            expect(! r.playback.isActive());
        }

        beginTest("Re-pitch at twice the loop tempo plays the slice in half the time");
        {
            Rig r;
            r.settings.mode = TempoMode::Match; r.settings.stretchMix = 0.0f; r.settings.loopTempo = 120.0;
            r.timeline.place(0, 0.0, 0.0);
            auto out = r.run(24000, 128, 0.0, 240.0);
            expectWithinAbsoluteError(out.getSample(0, 5000), 0.1f, 1.0e-4f);          // slice length 12000 at rate 2
            expectGreaterThan(rms(out, 11000, 500), 0.05);
            expectEquals(rms(out, 12400, 4000), 0.0);
        }

        beginTest("Stretch at 100 BPM for a 120 BPM loop makes the slice 1.2 times as long");
        {
            Rig r;
            r.settings.mode = TempoMode::Match; r.settings.stretchMix = 1.0f; r.settings.loopTempo = 120.0;
            r.timeline.place(0, 0.0, 0.0);
            auto out = r.run(40000, 128, 0.0, 100.0);
            expectGreaterThan(rms(out, 28000, 500), 0.05);                              // total 28800
            expectEquals(rms(out, 29200, 4000), 0.0);
        }

        beginTest("a trimmed hit skips the start of its slice; a retimed hit plays in the new time");
        {
            Rig r;
            r.hitsStartWithSliceSettings = false;   // keep the hit's own trim
            r.settings.loopTempo = 120.0;
            r.timeline.place(0, 0.0, 0.0);
            const int i = r.timeline.trimStart(0, 0.5, 1.0);                       // half a beat in: 12000 samples skipped
            expectEquals(i, 0);
            expectWithinAbsoluteError(r.timeline.blocks()[0].own.trimInBeats, 0.5f, 1.0e-6f);
            auto out = r.run(30000, 128, 0.0, 120.0);
            expectEquals(rms(out, 5000, 4000), 0.0);                               // the block starts half a beat in
            expectGreaterThan(rms(out, 15000, 500), 0.05);
            expectEquals(rms(out, 25000, 4000), 0.0);                              // only 12000 samples are left

            Rig q;
            q.hitsStartWithSliceSettings = false;
            q.settings.loopTempo = 120.0;
            q.timeline.place(0, 0.0, 0.0);
            q.timeline.retime(0, 0.0, 2.0, 1.0);                                   // one beat of slice stretched over two
            expectWithinAbsoluteError(q.timeline.blocks()[0].own.retime, 2.0f, 1.0e-6f);
            auto out2 = q.run(60000, 128, 0.0, 120.0);
            expectGreaterThan(rms(out2, 40000, 500), 0.05);                        // twice the 24000 samples
            expectEquals(rms(out2, 49500, 4000), 0.0);
        }

        beginTest("dropping a block onto a longer one splits the remainder; the origin is untouched");
        {
            const std::vector<double> beats { 4.0, 1.0 };
            Timeline origin(16.0);
            origin.place(0, 0.0, 0.0);                                  // a four-beat block
            origin.place(1, 8.0, 0.0);                                  // a one-beat block to drop
            Timeline t = origin;
            const int at = t.drop(1, 1.0, false, beats);                // one beat in, over the long block
            expectEquals(t.size(), 3);
            expectEquals(at, 1);
            expectWithinAbsoluteError(t.blocks()[0].length, 1.0, 1.0e-9);                       // the head is shortened
            expectWithinAbsoluteError(t.blocks()[2].start, 2.0, 1.0e-9);                        // the remainder starts after the drop
            expectWithinAbsoluteError((double) t.blocks()[2].own.trimInBeats, 2.0, 1.0e-6);     // and skips what was covered
            expectEquals(origin.size(), 2);                                                     // the drag's origin keeps both blocks
            Timeline over = origin;
            over.drop(1, 0.0, false, beats);                            // onto the start of the long block
            expectEquals(over.size(), 2);                               // the dropped block plus the rest of the long one
            expectWithinAbsoluteError(over.blocks()[1].start, 1.0, 1.0e-9);
        }

        beginTest("a block longer than its slice runs on into the audio after it");
        {
            Rig r;
            r.settings.loopTempo = 120.0;
            r.timeline.place(0, 0.0, 0.0);
            r.timeline.setBlockLength(0, 2.0, 0.0);                     // two beats over a one-beat slice
            auto out = r.run(60000, 128, 0.0, 120.0);
            expectWithinAbsoluteError(out.getSample(0, 12000), 0.1f, 1.0e-4f);   // the slice itself
            expectWithinAbsoluteError(out.getSample(0, 36000), 0.2f, 1.0e-4f);   // then the next slice's audio
        }

        beginTest("cropping the loop keeps the slices that remain; the hits of a slice that went are silent");
        {
            Rig r;
            std::vector<int> map;
            // Keep the middle: half of slice 1 to half of slice 2 (samples 36000..60000).
            const auto cut = r.slices.cropped(36000, 60000, map);
            expectEquals(cut.loopLength(), 24000);
            expectEquals(cut.size(), 2);                                // the second half of slice 1, the first half of slice 2
            expectEquals(cut.marker(1).position, 12000);
            expectEquals(map[0], -1);
            expectEquals(map[1], 0);
            expectEquals(map[2], 1);
            expectEquals(map[3], -1);

            Timeline t(4.0);
            t.place(0, 0.0, 0.0);
            t.place(1, 1.0, 0.0);
            t.remapSlices(map);
            expectEquals(t.blocks()[0].slice, -1);                      // kept on the timeline without a slice
            expectEquals(t.blocks()[1].slice, 0);

            // The cut loop plays the hit with a slice and stays silent for the other.
            juce::AudioBuffer<float> audio(1, 24000);
            for (int i = 0; i < 24000; ++i) audio.setSample(0, i, r.audio.getSample(0, 36000 + i));
            Rig c;
            c.audio.setSize(1, 24000); c.audio.copyFrom(0, 0, audio, 0, 0, 24000);
            c.slices = cut;
            c.timeline = t;
            c.loop.audio = &c.audio; c.loop.slices = &c.slices; c.loop.timeline = &c.timeline;
            auto out = c.run(96000, 128);
            expectEquals(rms(out, 100, 4000), 0.0);                                  // beat 0: the cropped-off hit
            expectGreaterThan(rms(out, 24000 + 100, 4000), 0.05);                    // beat 1: slice 0 of the cut loop
        }

        beginTest("a hit's volume (the dragged top of its block) scales its level, 0 is silent");
        {
            Rig r;
            r.hitsStartWithSliceSettings = false;
            r.timeline.place(0, 0.0, 0.0);
            r.timeline.place(1, 1.0, 0.0);
            r.timeline.place(2, 2.0, 0.0);
            auto half = r.timeline.blocks()[0].own; half.volume = 0.5f; r.timeline.setSettings(0, half);
            auto none = r.timeline.blocks()[2].own; none.volume = 0.0f; r.timeline.setSettings(2, none);
            auto out = r.run(96000, 128);
            expectWithinAbsoluteError(out.getSample(0, 12000), 0.05f, 1.0e-4f);   // slice 0 holds 0.1: half of it
            expectWithinAbsoluteError(out.getSample(0, 36000), 0.2f, 1.0e-4f);    // slice 1 untouched
            expectEquals(out.getSample(0, 60000), 0.0f);                         // slice 2 at volume 0
            auto s2 = sanitised(half); s2.volume = 7.0f;
            expectEquals(sanitised(s2).volume, 1.0f);
        }

        beginTest("a hit's dragged envelope corners fade it in and out as a fraction of its length");
        {
            Rig r;
            r.hitsStartWithSliceSettings = false;
            r.timeline.place(1, 1.0, 0.0);
            auto shaped = r.timeline.blocks()[0].own; shaped.envIn = 0.5f; shaped.envOut = 0.5f; r.timeline.setSettings(0, shaped);
            auto out = r.run(96000, 128);
            expectWithinAbsoluteError(out.getSample(0, 24000 + 6000), 0.1f, 2.0e-3f);    // half way up the fade in
            expectWithinAbsoluteError(out.getSample(0, 24000 + 12000), 0.2f, 2.0e-3f);   // the corners meet: full level
            expectWithinAbsoluteError(out.getSample(0, 24000 + 18000), 0.1f, 2.0e-3f);   // half way down the fade out
            auto bent = shaped; bent.envInCurve = 1.0f; r.timeline.setSettings(0, bent);
            auto outBent = r.run(96000, 128);
            expectWithinAbsoluteError(outBent.getSample(0, 24000 + 6000), 0.2f * 0.0625f, 2.0e-3f);   // x^4 at half way: a slow start
            expectWithinAbsoluteError(outBent.getSample(0, 24000 + 18000), 0.1f, 2.0e-3f);           // the fade-out stays straight
            auto bad = shaped; bad.envIn = 0.8f; bad.envOut = 0.9f;
            expectLessOrEqual(sanitised(bad).envIn + sanitised(bad).envOut, 1.0f);
        }

        beginTest("a block on the downbeat sounds on every pass when the timeline's length is not a whole number of samples");
        {
            // 126 bpm: four beats are 91428.57 samples, so the wrap falls between two samples.
            Rig r;
            r.hitsStartWithSliceSettings = false;
            r.timeline.place(0, 0.0, 0.0);
            auto out = r.run(3 * 91429, 128, 0.0, 126.0);
            int passes = 0;
            for (int pass = 0; pass < 3; ++pass)
            {
                const int at = (int) std::lround(pass * 4.0 * 60.0 / 126.0 * kSr);
                if (std::abs(out.getSample(0, at + 200)) > 0.01f) ++passes;
            }
            expectEquals(passes, 3);
        }

        beginTest("a block near the end of the timeline sounds again in the next pass");
        {
            Rig r;
            r.timeline.place(1, 3.5, 0.0);                              // half a beat before the wrap
            auto out = r.run(2 * 96000, 200);
            expectWithinAbsoluteError(out.getSample(0, 84000 + 500), 0.2f, 1.0e-5f);   // pass one
            expectWithinAbsoluteError(out.getSample(0, 96000 + 84000 + 500), 0.2f, 1.0e-5f);   // pass two
        }

        beginTest("a hit's settings are offsets added to the layer's values (pan, amp attack), held to the control's range");
        {
            // Pan: the layer pans hard left, the hit adds a full swing right: they cancel to the centre.
            auto sides = [](float layerPan, float hitPan)
            {
                Rig r;
                r.hitsStartWithSliceSettings = false;
                r.timeline.place(1, 0.0, 0.0);
                SliceSettings own; own.pan = hitPan;
                r.timeline.setSettings(0, own);
                r.settings.pan = layerPan;
                juce::AudioBuffer<float> out(2, 12000);
                out.clear();
                PhraseClock c;
                c.playing = true; c.bpm = 120.0; c.sampleRate = kSr; c.beatsPerSample = 120.0 / 60.0 / kSr; c.startBeat = 0.0;
                r.playback.render(r.loop, r.settings, c, out, 12000);
                return std::pair<double, double> { out.getRMSLevel(0, 2000, 8000), out.getRMSLevel(1, 2000, 8000) };
            };
            const auto leftOnly = sides(-1.0f, 0.0f), centred = sides(-1.0f, 1.0f);
            expectGreaterThan(leftOnly.first, 0.1);
            expectLessThan(leftOnly.second, 0.001);                       // the layer alone: all left
            expectWithinAbsoluteError(centred.first, centred.second, 0.001);   // plus the hit's +1: back in the centre
            const auto clamped = sides(1.0f, 1.0f);                       // 1 + 1 is held to the hard right
            expectLessThan(clamped.first, 0.001);

            // Amp attack: the layer sets 50 ms; a hit with -50 ms plays at once, a hit with +50 ms takes 100 ms.
            auto early = [](float layerAttack, float hitOffset)
            {
                Rig r;
                r.hitsStartWithSliceSettings = false;
                r.timeline.place(1, 0.0, 0.0);
                SliceSettings own; own.ampA = hitOffset;
                r.timeline.setSettings(0, own);
                r.settings.ampA = layerAttack;
                return rms(r.run(4800, 256), 0, 480);                     // the first 10 ms
            };
            expectGreaterThan(early(50.0f, -50.0f), 3.0 * early(50.0f, 0.0f));   // the offset cancels the layer's attack
            expectGreaterThan(early(50.0f, 0.0f), 1.6 * early(50.0f, 50.0f));    // and a longer one slows it further (twice the attack, half the level)

            // Absolute: the hit's value replaces the layer's attack whatever it is, and the offset kept beside it is ignored.
            auto earlyAbs = [](float layerAttack, float absAttack)
            {
                Rig r;
                r.hitsStartWithSliceSettings = false;
                r.timeline.place(1, 0.0, 0.0);
                SliceSettings own; own.ampA = 400.0f;                     // the offset it keeps for switching back: must not act
                own.ov[(int) OverrideField::AmpA] = { true, absAttack };
                r.timeline.setSettings(0, own);
                r.settings.ampA = layerAttack;
                return rms(r.run(4800, 256), 0, 480);
            };
            expectWithinAbsoluteError(earlyAbs(50.0f, 0.0f), early(50.0f, -50.0f), 0.01);   // Absolute 0 is the same as cancelling the layer's 50
            expectWithinAbsoluteError(earlyAbs(500.0f, 0.0f), earlyAbs(0.0f, 0.0f), 0.01);   // the layer's own attack no longer matters
            expectGreaterThan(earlyAbs(0.0f, 0.0f), 1.6 * earlyAbs(0.0f, 50.0f));            // and an Absolute 50 ms is slower
        }

        beginTest("the newest hit's insert offsets are reported while it sounds");
        {
            Rig r;
            r.hitsStartWithSliceSettings = false;
            r.timeline.place(0, 0.0, 0.0);
            SliceSettings own; own.insOff[1] = 0.25f; own.insOff[3] = -0.5f;
            r.timeline.setSettings(0, own);
            float off[4] = { 9, 9, 9, 9 };
            unsigned absMask = 0;
            expect(! r.playback.insertOffsets(off, absMask));                      // nothing sounding yet
            expectEquals(off[1], 0.0f);
            r.run(2400, 256);
            expect(r.playback.insertOffsets(off, absMask));
            expectEquals(off[0], 0.0f); expectEquals(off[1], 0.25f); expectEquals(off[2], 0.0f); expectEquals(off[3], -0.5f);
        }

        beginTest("an insert knob a hit sets Absolute reaches the insert as its value, not an offset");
        {
            Rig r;
            r.hitsStartWithSliceSettings = false;
            r.timeline.place(0, 0.0, 0.0);
            SliceSettings own; own.insOff[1] = 0.25f;
            own.ov[(int) OverrideField::Ins3] = { true, 0.8f };
            own.ov[(int) OverrideField::Send2] = { true, 0.6f };
            r.timeline.setSettings(0, own);
            float off[4] = { 9, 9, 9, 9 }, send[3] = { 9, 9, 9 };
            unsigned insMask = 0, sendMask = 0;
            r.run(2400, 256);
            expect(r.playback.insertOffsets(off, insMask));
            expectEquals(off[1], 0.25f); expectEquals(off[3], 0.8f);
            expectEquals((int) insMask, 8);                                                   // only knob 3 is Absolute
            expect(r.playback.sendOffsets(send, sendMask));
            expectEquals(send[2], 0.6f); expectEquals((int) sendMask, 4);
        }

        beginTest("a Stretch phrase renders without touching the heap");
        {
            Rig r;
            r.settings.mode = TempoMode::Match; r.settings.stretchMix = 1.0f; r.settings.loopTempo = 120.0;
            r.timeline.place(0, 0.0, 0.0); r.timeline.place(1, 1.0, 0.0); r.timeline.place(2, 2.0, 0.0);
            r.run(512, 128, 0.0, 100.0);                                // warm-up of the first block
            juce::AudioBuffer<float> out(1, 96000);
            out.clear();
            mu_alloc::Armed region;
            r.runInto(out, 128, 0.0, 100.0);
            region.stop();
            expectEquals(region.allocs(), 0, "the phrase scheduler or the stretch touched the heap");
        }
    }
};

static PhraseTests phraseTests;
