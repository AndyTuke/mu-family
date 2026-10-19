// Layer-extraction safety net for mu-Clid, part 2: pins. Each test describes what the tree does about something
// a stage moves (the retired-engine tails and when they stop and are freed, what a count-reducing preset does to
// them, how long the longest one lives, a structural edit mid-play, a block larger than the prepared size, where a
// trigger lands in a block, where sample files are read, and the order of the MIDI a block emits). Written on the
// unmodified tree; a stage that moves the behaviour must pass them unedited, and a stage that has to edit one has
// changed behaviour, which goes to Andy as a deliberate, signed update. E5.5e (the tails into the shared RetireBin)
// is such an update for P2 (stop at Done, the flush at prepare), P3 (count policy CutAtCount) and P4 (30 s cap).
//
// Part 1, the bit-exact golden renders, is GoldenRenderTests.cpp.

#include "GoldenTestRig.h"
#include "Audio/MidiOutputEngine.h"
#include "Audio/VoiceEngine.h"
#include "Tests/GoldenRender.h"
#include "Tests/RetireIsolation.h"   // mu-core: the generic twin test

namespace
{
    using Buf = juce::AudioBuffer<float>;
    using mu_golden_rig::OwnedProcessor;
    using mu_golden_rig::rp;
    using mu_golden_rig::setParam;
    using mu_golden_rig::useRhythms;
    using mu_golden_rig::useSample;

    constexpr double kSr = 48000.0;
    constexpr int kStepFrames = 6000;   // one 16th at 120 bpm and 48 kHz

    float peakOf(const Buf& b, int from, int to)
    {
        float p = 0.0f;
        for (int c = 0; c < b.getNumChannels(); ++c)
            p = juce::jmax(p, b.getMagnitude(c, from, juce::jmax(0, to - from)));
        return p;
    }

    // A recorded MIDI message with its absolute frame.
    struct MidiEv
    {
        juce::int64 frame; int status; int data1;
        bool isOn()  const { return (status & 0xf0) == 0x90; }
        bool isOff() const { return (status & 0xf0) == 0x80; }
        int channel() const { return (status & 0x0f) + 1; }
    };

    // A real PluginProcessor with a mock host, prepared, driven block by block on the test thread. The
    // deferred-work drain runs after every block (OwnedProcessor), as in the golden renders.
    struct Rig
    {
        explicit Rig(int preparedBlock = 512) : prepared(preparedBlock)
        {
            host.bpm = 120.0;
            p.setPlayHead(&host);
            p.setRateAndBufferSizeDetails(kSr, preparedBlock);
            p.prepareToPlay(kSr, preparedBlock);
            mu_golden_rig::writeFixtureSamples(p);
        }
        ~Rig() { p.setPlayHead(nullptr); p.releaseResources(); }

        // One host block of `n` frames; the MIDI it emitted is appended to `midiLog`.
        Buf block(int n)
        {
            Buf work(2, juce::jmax(1, n));
            work.clear();
            Buf view(work.getArrayOfWritePointers(), 2, n);
            juce::MidiBuffer midi;
            host.timeInSamples = frame;
            p.processBlock(view, midi);
            for (const auto meta : midi)
                midiLog.push_back({ frame + meta.samplePosition, meta.data[0], meta.numBytes > 1 ? meta.data[1] : 0 });
            if (host.playing) host.ppq += (double) n * host.bpm / (60.0 * kSr);
            frame += n;
            Buf out(2, n);
            for (int c = 0; c < 2; ++c) out.copyFrom(c, 0, view, c, 0, n);
            return out;
        }

        Buf render(int frames, int blockSize = 512)
        {
            Buf all(2, frames);
            for (int at = 0; at < frames; at += blockSize)
            {
                const int n = juce::jmin(blockSize, frames - at);
                const auto b = block(n);
                for (int c = 0; c < 2; ++c) all.copyFrom(c, at, b, c, 0, n);
            }
            return all;
        }

        // The recorded MIDI with a given note-on/off kind and note, in order.
        std::vector<MidiEv> events(bool on, int note) const
        {
            std::vector<MidiEv> out;
            for (const auto& e : midiLog)
                if ((on ? e.isOn() : e.isOff()) && e.data1 == note) out.push_back(e);
            return out;
        }

        juce::File dir(const char* name) const { return p.getRhythmsDir().getChildFile(name); }

        OwnedProcessor p;
        mu_golden::MockHost host;
        int prepared;
        juce::int64 frame = 0;
        std::vector<MidiEv> midiLog;
    };

    // Ring A as given, ring B matched to it with no hits, so the rhythm's loop is `steps` long (the loop is
    // the LCM of the two rings, and a staged swap commits at its end).
    void ringA(mu_clid::PluginProcessor& p, int r, int steps, int hits, int rot = 0)
    {
        setParam(p.apvts, rp(r, "stepsA"), (float) steps);
        setParam(p.apvts, rp(r, "hitsA"), (float) hits);
        setParam(p.apvts, rp(r, "rotA"), (float) rot);
        setParam(p.apvts, rp(r, "stepsB"), (float) steps);
        setParam(p.apvts, rp(r, "hitsB"), 0.0f);
    }

    // A Karplus insert at full feedback on a high note, so the loop rings for minutes.
    void karplus(mu_clid::PluginProcessor& p, int r, int note = 11)
    {
        setParam(p.apvts, rp(r, "drvChar"), 11.0f);
        setParam(p.apvts, rp(r, "insP1"), (float) note / 11.0f);
        setParam(p.apvts, rp(r, "insP2"), 1.0f);    // octave 3
        setParam(p.apvts, rp(r, "insP3"), 1.0f);    // feedback 100 percent
        setParam(p.apvts, rp(r, "insP4"), 1.0f);    // loop low-pass wide open
    }

    void midiMode(mu_clid::PluginProcessor& p, int r, int note, int chan = 1)
    {
        setParam(p.apvts, rp(r, "outMode"), 1.0f);
        setParam(p.apvts, rp(r, "midiNote"), (float) note);
        setParam(p.apvts, rp(r, "midiChan"), (float) chan);
    }

    // Saves a silent 4-step rhythm preset (no sample, no hits) from rhythm 0 as it is now, before the test configures it.
    juce::File saveSilentPreset(Rig& rig)
    {
        ringA(rig.p, 0, 4, 0);
        const auto f = rig.dir("silent.muRhythm");
        rig.p.saveRhythmPresetToFile(0, f);
        return f;
    }

    // True when slot `slot`'s retire-bin row holds engine `e` and it still sounds (Parked or Draining).
    bool slotHolds(mu_clid::PluginProcessor& p, int slot, const VoiceEngine* e)
    {
        for (int k = 0; k < mu_core::RetireBin::kDepth; ++k)
            if (p.retiredEngineForTest(slot, k) == e) return true;
        return false;
    }

    int tailCount(mu_clid::PluginProcessor& p, int slot) { return p.retiredCountForTest(slot); }

    // The mixer's per-channel peak (post strip) as of the last block.
    float chPeak(mu_clid::PluginProcessor& p, int ch)
    {
        return juce::jmax(p.mixerEngine.channelPeaksL[ch].load(), p.mixerEngine.channelPeaksR[ch].load());
    }
}

struct MuClidExtractionPinTests : juce::UnitTest
{
    MuClidExtractionPinTests() : juce::UnitTest("mu-Clid extraction pins", "mu-clid-pins") {}

    void runTest() override
    {
        drainedTailPins();
        countPolicyPins();
        longestTailPins();
        editMidPlayPins();
        oversizePins();
        triggerSourcePins();
        sampleIoPins();
        midiOrderingPins();
    }

    // ── P2: a retired Karplus engine after isFullyDrained() (E5.5e: stop at Done) ──────────────────────
    void drainedTailPins()
    {
        beginTest("P2: a retired Karplus engine stops at Done: once it reports drained a 10 ms declick closes it, then it renders nothing and the bin frees it");
        {
            Rig rig;
            auto& p = rig.p;
            const auto silent = saveSilentPreset(rig);
            useSample(p, 0, "Kick 01.wav");
            ringA(p, 0, 4, 1);
            karplus(p, 0);

            const VoiceEngine* tail = nullptr;
            int placedBlock = -1, doneBlock = -1, freedBlock = -1;
            float peakBeforeDone = 0.0f, peakAfterDone = 0.0f;
            Buf lastBlocks(2, 4 * 512);   // the four blocks before Done, the declick's end among them
            lastBlocks.clear();
            for (int b = 0; b < 400; ++b)
            {
                if (b == 4) p.stageRhythmPreset(0, silent);
                const auto out = rig.block(512);
                if (doneBlock < 0)
                {
                    for (int c = 0; c < 2; ++c)
                    {
                        std::memmove(lastBlocks.getWritePointer(c), lastBlocks.getReadPointer(c) + 512, 3 * 512 * sizeof (float));
                        lastBlocks.copyFrom(c, 3 * 512, out, c, 0, 512);
                    }
                }
                if (tail == nullptr && p.retiredEngineForTest(0, 0) != nullptr) { tail = p.retiredEngineForTest(0, 0); placedBlock = b; }
                if (tail != nullptr && doneBlock < 0 && ! slotHolds(p, 0, tail)) doneBlock = b;
                if (doneBlock >= 0 && freedBlock < 0 && p.retiredStateForTest(0, 0) == mu_core::RetireBin::Empty) freedBlock = b;
                if (tail != nullptr && doneBlock < 0) peakBeforeDone = juce::jmax(peakBeforeDone, peakOf(out, 0, 512));
                if (doneBlock >= 0 && b > doneBlock) peakAfterDone = juce::jmax(peakAfterDone, peakOf(out, 0, 512));
            }
            logMessage("P2 retired at block " + juce::String(placedBlock) + ", Done at " + juce::String(doneBlock)
                       + " (" + juce::String(doneBlock - placedBlock) + " blocks later), freed by block " + juce::String(freedBlock)
                       + "; peak while it sounded " + juce::String(peakBeforeDone, 6) + ", after Done " + juce::String(peakAfterDone, 6));
            expect(placedBlock > 0 && doneBlock > placedBlock, "the engine is retired, then reports drained");
            expect(std::abs((doneBlock - placedBlock) - (int) (2.0 * kSr / 512.0)) <= 3, "drained about 2 s of process() calls after the retire (the drain budget)");
            expect(peakBeforeDone > 0.05f, "the full-feedback Karplus loop sounded while the tail lived");
            expectEquals(peakAfterDone, 0.0f, "once Done, the output is exactly zero: no block renders a drained tail");
            // The declick: the tail's last 24 non-zero frames are within the ramp's last 5 %, so it ends in a fade, not a step.
            int last = -1;
            for (int i = 0; i < lastBlocks.getNumSamples(); ++i)
                if (lastBlocks.getSample(0, i) != 0.0f || lastBlocks.getSample(1, i) != 0.0f) last = i;
            const float endLevel = last >= 24 ? peakOf(lastBlocks, last - 24, last + 1) : 1.0f;
            logMessage("P2 level over the tail's last 24 frames: " + juce::String(endLevel, 6));
            expect(endLevel < 0.05f * peakBeforeDone + 1.0e-6f, "the drained tail is faded out, not cut");
            expect(freedBlock >= doneBlock && freedBlock <= doneBlock + 1, "the deferred-work pass the Done asks for frees it at once");
        }

        beginTest("P2: a tail with nothing ringing drains on its measured silence, in the same time whatever size the host calls with");
        {
            auto drainBlocks = [this](int hostBlock)
            {
                Rig rig(512);
                auto& p = rig.p;
                const auto silent = saveSilentPreset(rig);
                useSample(p, 0, "Kick 01.wav");
                ringA(p, 0, 4, 1);
                const VoiceEngine* tail = nullptr;
                int placed = -1, done = -1;
                for (int b = 0; b < 4000 && done < 0; ++b)
                {
                    if (b == 4) p.stageRhythmPreset(0, silent);
                    rig.block(hostBlock);
                    if (tail == nullptr && p.retiredEngineForTest(0, 0) != nullptr) { tail = p.retiredEngineForTest(0, 0); placed = b; }
                    if (tail != nullptr && ! slotHolds(p, 0, tail)) done = b;
                }
                logMessage("P2 host block " + juce::String(hostBlock) + ": drained " + juce::String(done - placed) + " calls after the retire");
                return done - placed;
            };
            // The Kick has nothing that rings past its envelope, so its tail ends on measured silence (50 ms below
            // -100 dBFS after the 0.5 s release), not on the 2 s budget: the same time whatever the host block size.
            const double atPrepared = drainBlocks(512) * 512.0 / kSr;
            const double atQuarter  = drainBlocks(128) * 128.0 / kSr;
            expect(atPrepared > 0.45 && atPrepared < 0.7, "512-frame calls: drained on silence, about 0.55 s after the retire");
            expect(atQuarter > 0.45 && atQuarter < 0.7, "128-frame calls: drained on silence, about 0.5 s after the retire");
            expect(std::abs(atPrepared - atQuarter) < 0.06, "the drain time does not follow the host block size");
        }

        beginTest("P2/P4: a retired engine that holds no sample renders nothing and ends at once (it no longer holds its entry)");
        {
            Rig rig;
            auto& p = rig.p;
            const auto silent = saveSilentPreset(rig);
            ringA(p, 0, 4, 4);   // the default rhythm has no sample
            const VoiceEngine* tail = nullptr;
            int placed = -1, ended = -1;
            for (int b = 0; b < 3000 && ended < 0; ++b)
            {
                if (b == 4) p.stageRhythmPreset(0, silent);
                rig.block(512);
                if (tail == nullptr && p.retiredEngineForTest(0, 0) != nullptr) { tail = p.retiredEngineForTest(0, 0); placed = b; }
                if (tail != nullptr && ! slotHolds(p, 0, tail)) ended = b;
            }
            const double life = (double) (ended - placed) * 512.0 / kSr;
            logMessage("P2/P4 a sample-less retired engine ended " + juce::String(life, 3) + " s after the retire");
            expect(ended > placed && life < 0.05, "it ends within its first tail blocks (the declick)");
        }

        beginTest("P2: prepareToPlay flushes the retire bin: a ringing tail is cut and freed");
        {
            Rig rig;
            auto& p = rig.p;
            const auto silent = saveSilentPreset(rig);
            useSample(p, 0, "Kick 01.wav");
            ringA(p, 0, 4, 1);
            karplus(p, 0);
            for (int b = 0; b < 100; ++b)
            {
                if (b == 4) p.stageRhythmPreset(0, silent);
                rig.block(512);
            }
            const auto before = rig.block(512);
            expectEquals(tailCount(p, 0), 1, "a tail is retired and ringing");
            expect(peakOf(before, 0, 512) > 0.05f, "audibly");
            p.prepareToPlay(kSr, 512);
            expectEquals(tailCount(p, 0), 0, "the prepare cut it");
            expect(p.retiredStateForTest(0, 0) == mu_core::RetireBin::Empty, "and freed it (nothing processes during a prepare)");
            const auto after = rig.render(4 * 512);
            logMessage("P2 prepareToPlay on a ringing tail: peak before " + juce::String(peakOf(before, 0, 512), 5) + ", after " + juce::String(peakOf(after, 0, 4 * 512), 6));
            expectEquals(peakOf(after, 0, 4 * 512), 0.0f, "the tail is gone");
        }
    }

    // ── P3: tails when the layer count moves (E5.5e: count policy CutAtCount) ─────────────────────────────
    void countPolicyPins()
    {
        // Three rhythms of four steps, each on a Karplus that rings for minutes; at block 4 each is swapped for a
        // silent rhythm, so by block 60 three tails ring, one per slot.
        auto setup = [](Rig& rig)
        {
            auto& p = rig.p;
            const auto silent = saveSilentPreset(rig);
            useRhythms(p, 3);
            for (int r = 0; r < 3; ++r)
            {
                useSample(p, r, "Kick 01.wav");
                ringA(p, r, 4, 1);
                karplus(p, r, 3 + 3 * r);
            }
            return silent;
        };

        beginTest("P3: a count-reducing full preset cuts the tails of the dropped slots; a regrow starts silent");
        {
            Rig rig;
            auto& p = rig.p;
            const auto silentFull = p.getPresetsDir().getChildFile("silent1.muClid");
            // A one-rhythm silent full preset, written while the processor still holds only its default rhythm.
            ringA(p, 0, 4, 0);
            p.saveFullPresetTo(silentFull);
            const auto silent = setup(rig);

            std::array<const VoiceEngine*, 3> tails {};
            float postRegrow[3] = {};
            for (int b = 0; b < 420; ++b)
            {
                if (b == 4) for (int r = 0; r < 3; ++r) p.stageRhythmPreset(r, silent);
                if (b == 60)
                {
                    for (int r = 0; r < 3; ++r) tails[(size_t) r] = p.retiredEngineForTest(r, 0);
                    p.loadPreset(silentFull);   // playing: staged, lands at the next loop boundary
                }
                if (b == 150)
                {
                    expectEquals(p.getNumRhythms(), 1, "the full preset has landed: one rhythm");
                    expect(! slotHolds(p, 1, tails[1]) && ! slotHolds(p, 2, tails[2]), "the tails of the dropped slots 1 and 2 were cut at the count store");
                    expectEquals(chPeak(p, 1), 0.0f, "slot 1's channel is silent");
                    expectEquals(chPeak(p, 2), 0.0f, "slot 2's channel is silent");
                    expect(chPeak(p, 0) > 0.01f, "slot 0's own tail goes on ringing");
                }
                if (b == 400) { p.addLayer(); p.addLayer(); }
                rig.block(512);
                if (b == 401) for (int r = 0; r < 3; ++r) postRegrow[r] = chPeak(p, r);
            }
            logMessage("P3 just after the regrow: channel peaks " + juce::String(postRegrow[1], 4) + "/" + juce::String(postRegrow[2], 4));
            expect(tails[0] != nullptr && tails[1] != nullptr && tails[2] != nullptr, "all three tails existed");
            expect(tailCount(p, 1) == 0 && tailCount(p, 2) == 0, "after the regrow the slots hold no tail");
            expectEquals(postRegrow[1], 0.0f, "the regrown slot 1, whose rhythm is silent, is silent (no stale tail resumes)");
            expectEquals(postRegrow[2], 0.0f, "and so is slot 2");
        }

        beginTest("P3: removeLayer shifts the tails down with their rhythms; the removed slot's tail is cut and the top slot is empty");
        {
            Rig rig;
            auto& p = rig.p;
            const auto silent = setup(rig);
            std::array<const VoiceEngine*, 3> tails {};
            for (int b = 0; b < 100; ++b)
            {
                if (b == 4) for (int r = 0; r < 3; ++r) p.stageRhythmPreset(r, silent);
                if (b == 60) for (int r = 0; r < 3; ++r) tails[(size_t) r] = p.retiredEngineForTest(r, 0);
                if (b == 70) p.removeLayer(0);
                rig.block(512);
            }
            expectEquals(p.getNumRhythms(), 2);
            expect(tails[0] != nullptr && tails[1] != nullptr && tails[2] != nullptr, "all three tails existed");
            expect(slotHolds(p, 0, tails[1]) && slotHolds(p, 1, tails[2]), "slot 1's and 2's tails now sit on slots 0 and 1");
            expectEquals(tailCount(p, 2), 0, "the vacated top slot holds none");
            logMessage("P3 channel peaks after removeLayer(0): " + juce::String(chPeak(p, 0), 4) + " " + juce::String(chPeak(p, 1), 4) + " " + juce::String(chPeak(p, 2), 4));
            expect(chPeak(p, 0) > 0.01f && chPeak(p, 1) > 0.01f, "the shifted tails keep ringing in their new channels");
            expectEquals(chPeak(p, 2), 0.0f, "and the channel above is silent");
        }
    }

    // ── P4: the longest retired tail in the golden set ─────────────────────────────────────────────
    void longestTailPins()
    {
        struct Event { int block; std::function<void(Rig&)> apply; };

        // Loads a golden scenario's frozen state, replays its events on the 512-frame schedule and measures each
        // retired engine's life: blocks from the retire to the block it stopped sounding (Done), or to the end.
        struct TailStats { double longestDrained = 0.0; int undrainedWithSample = 0; int undrainedWithoutSample = 0; };
        auto longestTail = [this](const char* scenario, int blocks, const std::vector<Event>& events)
        {
            TailStats stats;
            Rig rig(1024);   // the golden scenarios prepare at 1024
            auto& p = rig.p;
            const auto text = juce::File(__FILE__).getParentDirectory().getParentDirectory().getParentDirectory().getParentDirectory()
                                  .getChildFile("tests/golden/mu-clid").getChildFile(scenario).getChildFile("state.xml").loadFileAsString();
            expect(text.isNotEmpty(), juce::String(scenario) + ": golden state.xml found");
            p.setStateInformation(text.toRawUTF8(), (int) text.getNumBytesAsUTF8());
            struct Life { const VoiceEngine* e = nullptr; int start = 0; };
            Life life[8][4];
            for (int b = 0; b < blocks; ++b)
            {
                for (const auto& ev : events) if (ev.block == b) ev.apply(rig);
                rig.block(512);
                for (int s = 0; s < 8; ++s)
                    for (int i = 0; i < 4; ++i)
                    {
                        auto& l = life[s][i];
                        const VoiceEngine* now = p.retiredEngineForTest(s, i);
                        if (now == l.e) continue;
                        if (l.e != nullptr)   // the entry's engine stopped sounding: drained (the bin freed it)
                            stats.longestDrained = juce::jmax(stats.longestDrained, (double) (b - l.start) * 512.0 / kSr);
                        l = Life{};
                        l.e = now;
                        l.start = b;
                    }
            }
            for (int s = 0; s < 8; ++s)
                for (int i = 0; i < 4; ++i)
                {
                    const auto& l = life[s][i];
                    if (l.e == nullptr) continue;
                    if (l.e->hasSample()) ++stats.undrainedWithSample;
                    else                  ++stats.undrainedWithoutSample;
                }
            return stats;
        };

        auto stage = [](int slot, const char* file)
        {
            return [slot, file](Rig& r) { r.p.stageRhythmPreset(slot, mu_golden_rig::presetsDir().getChildFile(file)); };
        };

        beginTest("P4: the longest retired tail in the golden set is the 12 s Pad Long sample played to its end, well under the 30 s cap");
        {
            const auto longS = longestTail("retire-long-amprel", 1407, { { 25, stage(0, "A9_hat.muRhythm") } });
            const auto clipS = longestTail("hotswap-clip", 563, { { 20, stage(1, "T11.muRhythm") },
                                                                 { 150, [](Rig& r) { r.p.launchClip(0, 1); } },
                                                                 { 260, stage(0, "A9_hat.muRhythm") } });
            logMessage("P4 retire-long-amprel: longest drained tail " + juce::String(longS.longestDrained, 2) + " s, undrained with sample "
                       + juce::String(longS.undrainedWithSample) + ", without " + juce::String(longS.undrainedWithoutSample)
                       + "; hotswap-clip: longest drained tail " + juce::String(clipS.longestDrained, 2) + " s, undrained with sample "
                       + juce::String(clipS.undrainedWithSample) + ", without " + juce::String(clipS.undrainedWithoutSample));
            expectEquals(longS.undrainedWithSample + clipS.undrainedWithSample, 0, "every retired engine that holds a sample drains before its scenario ends");
            expect(longS.longestDrained > 11.8 && longS.longestDrained < 12.2, "the amp-release-at-maximum engine plays its whole 12 s sample from the retrigger at the swap boundary, on past the retire");
            expect(clipS.longestDrained > 0.4 && clipS.longestDrained < 0.8, "the Kick engines of the hot-swap scenario drain on their measured silence after the release: about 0.55 s, well inside the 2 s budget");
            expect(juce::jmax(longS.longestDrained, clipS.longestDrained) < 30.0, "so the 30 s cap cuts nothing the golden set plays");
            expectEquals(longS.undrainedWithoutSample + clipS.undrainedWithoutSample, 0, "the engines that never held a sample (the slot's default engine, retired when the state loads) end at once");
        }
    }

    // ── P6: a structural edit mid-play ─────────────────────────────────────────────────────────────
    void editMidPlayPins()
    {
        // Rhythm 0 a held Pad Long note on step 0, rhythm 1 a MIDI note every fourth step, rhythm 2 a Kick on every other step.
        auto setup = [](Rig& rig)
        {
            auto& p = rig.p;
            useRhythms(p, 3);
            useSample(p, 0, "Pad Long.wav");
            ringA(p, 0, 16, 1);
            setParam(p.apvts, rp(0, "aEnvSus"), 100.0f);
            midiMode(p, 1, 60);
            ringA(p, 1, 16, 4);
            useSample(p, 2, "Kick 01.wav");
            ringA(p, 2, 16, 8);
        };
        auto hits = [](Rig& rig, int r) { return rig.p.sampleRhythm(r).playState.hitCount.load(); };

        beginTest("Blocks skipped during an edit are silent, fire no step, emit no MIDI and leave the held note and the owed note-off untouched");
        {
            Rig rig;
            setup(rig);
            setParam(rig.p.apvts, "ch2_mute", 1.0f);   // the Kick is not heard (it still fires), so only the held pad is compared across the skip
            const auto first = rig.block(512);
            const float lastBefore = first.getSample(0, 511);
            expect(rig.events(true, 60).size() == 1 && rig.events(true, 60)[0].frame == 0, "rhythm 1's note-on went out at frame 0");
            const int h0 = hits(rig, 0), h1 = hits(rig, 1), h2 = hits(rig, 2);

            rig.p.layerEditInFlight.store(true);   // what a running structural edit raises
            const auto skipped = rig.render(47 * 512);
            rig.p.layerEditInFlight.store(false);
            expectEquals(peakOf(skipped, 0, skipped.getNumSamples()), 0.0f, "skipped blocks are silent");
            expect(hits(rig, 0) == h0 && hits(rig, 1) == h1 && hits(rig, 2) == h2, "no step fired in the skipped window");
            expectEquals((int) rig.midiLog.size(), 1, "no MIDI went out, the owed note-off included");

            const auto resumed = rig.block(512);
            logMessage("P6 held note across the skip: last sample before " + juce::String(lastBefore, 5) + ", first after " + juce::String(resumed.getSample(0, 0), 5));
            expect(peakOf(resumed, 0, 512) > 0.05f, "the held pad is sounding in the first block after the skip (no note release)");
            expect(std::abs(resumed.getSample(0, 0) - lastBefore) < 0.03f, "its waveform continues from where it left off: the voice did not advance");
            const auto offs = rig.events(false, 60);
            expectEquals((int) offs.size(), 1, "the owed note-off is sent");
            if (offs.size() == 1)
                expectEquals((int) offs[0].frame, 48 * 512, "at the first sample of the first block after the edit (0.5 s late), ahead of the note-on of the step that block lands on");
            expect(hits(rig, 1) == h1 + 1 && hits(rig, 2) == h2 + 1, "the first running block fires the step the transport has landed on, once; the missed ones are not caught up");
            rig.render(10 * 512);
            expect(hits(rig, 1) == h1 + 1 && hits(rig, 2) == h2 + 1, "and nothing is fired late afterwards");
        }

        beginTest("A staged swap whose boundary falls inside skipped blocks commits in the first block that runs");
        {
            Rig rig;
            auto& p = rig.p;
            const auto silent = saveSilentPreset(rig);
            useSample(p, 0, "Kick 01.wav");
            ringA(p, 0, 16, 4);
            int committed = -1;
            for (int b = 0; b < 400; ++b)
            {
                if (b == 4) p.stageRhythmPreset(0, silent);
                if (b == 150) p.layerEditInFlight.store(true);
                if (b == 220) p.layerEditInFlight.store(false);
                rig.block(512);
                if (committed < 0 && p.retiredEngineForTest(0, 0) != nullptr) committed = b;
            }
            logMessage("P6 swap boundary at block ~188 inside the skipped blocks 150-219: committed at block " + juce::String(committed));
            expectEquals(committed, 220, "the missed boundary is acted on at once when blocks run again, not a loop later");
        }

        beginTest("removeLayer takes the owed MIDI note-off of the removed slot and sends it at the start of the next block");
        {
            Rig rig;
            setup(rig);
            rig.block(512);   // rhythm 1's note-on at frame 0, off still owed
            rig.p.removeLayer(1);
            rig.block(512);
            const auto offs = rig.events(false, 60);
            expectEquals((int) offs.size(), 1, "exactly one note-off for the removed slot's note");
            if (offs.size() == 1) expectEquals((int) offs[0].frame, 512, "at the first sample of the next block");
            expectEquals((int) offs.size() == 1 ? offs[0].channel() : 0, 1, "on its channel");
            rig.render(20 * 512);
            expectEquals((int) rig.events(false, 60).size(), 1, "and no second one follows from the discarded engine");
        }

        beginTest("A host stop does not release a sounding voice, and the MIDI note-off still goes out on time");
        {
            Rig rig;
            setup(rig);
            rig.block(512);
            rig.host.playing = false;
            const auto stopped = rig.render(10 * 512);
            const auto offs = rig.events(false, 60);
            logMessage("P6 stopped: pad peak " + juce::String(peakOf(stopped, 0, stopped.getNumSamples()), 4));
            expect(peakOf(stopped, 0, stopped.getNumSamples()) > 0.05f, "the held pad rings on after the stop");
            expectEquals((int) offs.size(), 1, "the owed note-off is sent");
            if (offs.size() == 1) expectEquals((int) offs[0].frame, 1 * 512 + 448, "at 20 ms after the note-on, counted from its block start");
        }
    }

    // ── P7: a block larger than the prepared maximum, at the engine level ────────────────────────
    void oversizePins()
    {
        beginTest("P7: VoiceEngine::process renders at most the prepared block and drops the rest; the engine advances by what was rendered");
        {
            mu_seam_test::TempStorage storage;
            auto make = [this]
            {
                auto v = std::make_unique<VoiceEngine>();
                v->prepareToPlay(kSr, 512);
                mu_golden_rig::writeTone(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mu-p7-fixture.wav"), 200.0, 1.0, 1.0);
                v->loadFile(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mu-p7-fixture.wav"));
                VoiceParams vp;
                vp.ampEnvSus = 1.0f;
                v->setParams(vp);
                v->trigger(false, false);
                return v;
            };
            auto over = make(), ref = make();
            Buf got(2, 1024), want(2, 512), second(2, 512), wantSecond(2, 512);
            got.clear(); want.clear(); second.clear(); wantSecond.clear();
            over->process(got, 1024);
            ref->process(want, 512);
            bool same = true;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 512; ++i) same = same && got.getSample(c, i) == want.getSample(c, i);
            expect(same, "the first 512 frames are exactly a prepared-size render");
            expect(peakOf(got, 0, 512) > 0.05f, "which sounds");
            expectEquals(peakOf(got, 512, 1024), 0.0f, "frames 512 to 1023 are left untouched, with no error");

            over->process(second, 512);
            ref->process(wantSecond, 512);
            same = true;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 512; ++i) same = same && second.getSample(c, i) == wantSecond.getSample(c, i);
            expect(same, "the engine advanced by 512 frames, not 1024: the next block continues from frame 512");
        }

        beginTest("P7: MidiOutputEngine counts its note hold in the frames of the block it is given, not the prepared size");
        {
            MidiOutputEngine e;
            e.prepare(kSr, 512);
            juce::MidiBuffer m;
            e.trigger(m, 0, 60, 1, 1.0f);
            m.clear();
            e.processBlock(m, 1024);   // 20 ms is 960 frames, so the note-off falls inside this oversize block
            expectEquals(m.getNumEvents(), 1);
            for (const auto meta : m) expectEquals(meta.samplePosition, 1024 - 64, "at frame 960 of the block");
        }

        beginTest("An oversize host block (1024 frames on a 256 prepared size) is rendered in chunks: no overrun, finite sound across the whole block");
        {
            mu_seam_test::TempStorage storage;
            Rig rig(256);
            rig.host.playing = true;
            useSample(rig.p, 0, "Kick 01.wav");
            ringA(rig.p, 0, 16, 16);
            const auto out = rig.block(1024);
            float peak = 0.0f, tailPeak = 0.0f;
            bool finite = true;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 1024; ++i)
                {
                    const float v = out.getSample(c, i);
                    finite = finite && std::isfinite(v);
                    peak = juce::jmax(peak, std::abs(v));
                    if (i >= 768) tailPeak = juce::jmax(tailPeak, std::abs(v));
                }
            expect(finite);
            expect(peak > 0.0f);
            expect(tailPeak > 0.0f, "the last chunk is rendered too, not dropped");
        }
    }

    // ── The trigger source and the step cursor ──────────────────────────────────────────────────────
    void triggerSourcePins()
    {
        beginTest("A step fires in the first block that STARTS at or after it: the trigger and its MIDI note-on sit at that block's first sample, for any block size");
        {
            for (const int blockSize : { 512, 64, 1, 480 })
            {
                Rig rig(512);
                useRhythms(rig.p, 1);
                midiMode(rig.p, 0, 60);
                ringA(rig.p, 0, 16, 16);   // a hit on every 16th
                rig.render(6 * kStepFrames, blockSize);
                const auto ons = rig.events(true, 60);
                bool ok = ons.size() >= 6;
                for (size_t s = 0; s < ons.size() && s < 6; ++s)
                {
                    const juce::int64 stepFrame = (juce::int64) s * kStepFrames;
                    const juce::int64 expected = ((stepFrame + blockSize - 1) / blockSize) * blockSize;
                    if (ons[s].frame != expected)
                    {
                        ok = false;
                        logMessage("block " + juce::String(blockSize) + " step " + juce::String((int) s) + ": note-on at " + juce::String(ons[s].frame) + ", expected " + juce::String(expected));
                    }
                }
                expect(ok, "block " + juce::String(blockSize) + ": each of the first six steps lands on the next block start at or after its frame");
            }
        }

        beginTest("A sample voice starts at the same place: its onset is the first sample of the triggering block");
        {
            Rig rig;
            useSample(rig.p, 0, "Kick 01.wav");
            ringA(rig.p, 0, 16, 1, 3);   // rotated off step 0
            const auto pat = rig.p.getRhythm(0).getCombinedPattern();
            int hitStep = -1;
            for (size_t s = 0; s < pat.size() && hitStep < 0; ++s) if (pat[s]) hitStep = (int) s;
            const auto out = rig.render((hitStep + 2) * kStepFrames);
            int onset = -1;
            for (int i = 0; i < out.getNumSamples() && onset < 0; ++i)
                if (std::abs(out.getSample(0, i)) > 1.0e-7f) onset = i;
            const int blockStart = ((hitStep * kStepFrames + 511) / 512) * 512;
            logMessage("trigger source: Kick on step " + juce::String(hitStep) + " (frame " + juce::String(hitStep * kStepFrames) + ") starts at " + juce::String(onset) + ", block start " + juce::String(blockStart));
            expect(hitStep > 0 && onset >= blockStart && onset <= blockStart + 2, "audio starts at the first sample of the block the step fires in, not at the step's own frame");
        }

        beginTest("The cursor: three rhythms of 16, 12 and 10 steps under a 32-step master loop hit exactly the steps of their own patterns, restarting at every master wrap");
        {
            Rig rig;
            auto& p = rig.p;
            useRhythms(p, 3);
            const int lens[3]  = { 16, 12, 10 }, hitsN[3] = { 5, 5, 3 }, rots[3] = { 0, 1, 2 };
            for (int r = 0; r < 3; ++r) { midiMode(p, r, 60 + r); ringA(p, r, lens[r], hitsN[r], rots[r]); }
            setParam(p.apvts, "mstrLoop", 2.0f);   // 32 steps
            rig.render(750 * 512);                 // two master loops
            bool ok = true;
            for (int r = 0; r < 3; ++r)
            {
                const auto pat = p.getRhythm(r).getCombinedPattern();
                std::vector<juce::int64> expected;
                for (int s = 0; s < 64; ++s)
                    if (pat[(size_t) ((s % 32) % (int) pat.size())])
                        expected.push_back(((juce::int64) s * kStepFrames + 511) / 512 * 512);
                std::vector<juce::int64> got;
                for (const auto& e : rig.events(true, 60 + r)) got.push_back(e.frame);
                if (got != expected)
                {
                    ok = false;
                    logMessage("cursor: rhythm " + juce::String(r) + " fired " + juce::String((int) got.size()) + " times, expected " + juce::String((int) expected.size()));
                }
            }
            expect(ok, "every rhythm fires on the block of exactly the steps (step mod 32) mod its pattern length selects");
        }

        // A 12-step MIDI rhythm (note 60) is swapped, at its loop end (frame 72000), for an 8-step rhythm (note 72,
        // hits on steps 0, 2 and 5 of its pattern). The old pattern is rotated so that its boundary step is a hit
        // (the commit lands in a step the old pattern sounded) or a rest. Returns the new note-on frames.
        auto swapRun = [](bool boundaryStepSounds, std::vector<bool>& newPattern)
        {
            Rig rig;
            auto& p = rig.p;
            midiMode(p, 0, 72);
            ringA(p, 0, 8, 3);
            const auto donor = rig.dir("donor.muRhythm");
            p.saveRhythmPresetToFile(0, donor);
            newPattern = p.getRhythm(0).getCombinedPattern();
            midiMode(p, 0, 60);
            for (int rot = 0; rot < 12; ++rot)
            {
                ringA(p, 0, 12, 5, rot);
                if (p.getRhythm(0).getCombinedPattern()[0] == boundaryStepSounds) break;
            }
            setParam(p.apvts, "mstr_swap", 1.0f);   // OnLayerLoop
            for (int b = 0; b < 4; ++b) rig.block(512);
            p.stageRhythmPreset(0, donor);
            rig.render(480 * 512);
            std::vector<juce::int64> frames;
            for (const auto& e : rig.events(true, 72)) frames.push_back(e.frame);
            return frames;
        };
        auto blockAtOrAfter = [](juce::int64 frame) { return (frame + 511) / 512 * 512; };
        const juce::int64 boundary = 12 * kStepFrames;   // the old rhythm's loop end

        beginTest("A hot swap committed in a step the old pattern sounded absorbs the new pattern's step 0; the rest run from the boundary");
        {
            std::vector<bool> pat;
            const auto got = swapRun(true, pat);
            std::vector<juce::int64> expected;
            for (int s = 1; s < 24; ++s)
                if (pat[(size_t) (s % (int) pat.size())]) expected.push_back(blockAtOrAfter(boundary + (juce::int64) s * kStepFrames));
            juce::String line = "hot swap (boundary sounded): got";
            for (size_t i = 0; i < 6 && i < got.size(); ++i) line << " " << got[i];
            logMessage(line);
            expect(pat[0], "the new pattern has a hit on its step 0");
            expect(got.size() >= 5 && expected.size() >= 5 && std::equal(expected.begin(), expected.begin() + 5, got.begin()),
                   "the first new hit is the pattern's step 2, counted from the boundary; step 0 was not sounded");
        }

        beginTest("A hot swap committed in a step the old pattern rested on fires the new pattern's step 0 at once, in the block after the boundary");
        {
            std::vector<bool> pat;
            const auto got = swapRun(false, pat);
            juce::String line = "hot swap (boundary rest): got";
            for (size_t i = 0; i < 6 && i < got.size(); ++i) line << " " << got[i];
            logMessage(line);
            std::vector<juce::int64> expectedAfter;
            for (int s = 1; s < 24; ++s)
                if (pat[(size_t) (s % (int) pat.size())]) expectedAfter.push_back(blockAtOrAfter(boundary + (juce::int64) s * kStepFrames));
            expect(got.size() >= 6 && expectedAfter.size() >= 5, "the new pattern plays");
            if (got.size() >= 6 && expectedAfter.size() >= 5)
            {
                expect(got[0] >= boundary && got[0] <= boundary + 2 * 512, "step 0 sounds within a block or two of the boundary");
                expect(std::equal(expectedAfter.begin(), expectedAfter.begin() + 5, got.begin() + 1), "and the rest follow the pattern's steps counted from the boundary");
            }
        }
    }

    // ── Sample I/O happens when a swap is staged, never when it commits ───────────────────────────────
    void sampleIoPins()
    {
        beginTest("A staged rhythm preset has read its sample by the time stageRhythmPreset returns: deleting the file before the commit changes nothing");
        {
            Rig rig;
            auto& p = rig.p;
            useSample(p, 0, "Kick 01.wav");
            ringA(p, 0, 4, 1);
            setParam(p.apvts, rp(0, "aEnvRel"), 0.1f);
            rig.render(2 * 512);
            p.stageRhythmPreset(0, mu_golden_rig::presetsDir().getChildFile("T11.muRhythm"));   // names "Synth C 01.wav"
            const auto sample = p.getSamplesDir().getChildFile("Synth C 01.wav");
            expect(sample.existsAsFile(), "the file exists at stage time");
            sample.deleteFile();
            rig.render(500 * 512);   // past the commit and the old engine's drain
            logMessage("sample I/O: after the commit the rhythm's path is [" + p.samples.path(0) + "] missing " + juce::String((int) p.samples.isSampleMissing(0)));
            expect(p.samples.path(0).endsWith("Synth C 01.wav"), "the commit installed the staged sample path");
            expect(! p.samples.isSampleMissing(0), "and the engine already holds the sample's audio, read at stage time");
            const auto tone = rig.render(2 * kStepFrames * 4);
            expect(peakOf(tone, 0, tone.getNumSamples()) > 0.01f, "it plays");
        }
    }

    // ── The order of the MIDI a block emits ─────────────────────────────────────────────────────────
    void midiOrderingPins()
    {
        beginTest("Notes fired in one block come out in rhythm order, each as a note-on at the block's first sample");
        {
            Rig rig;
            auto& p = rig.p;
            useRhythms(p, 3);
            for (int r = 0; r < 3; ++r) { midiMode(p, r, 60 + 2 * r, 1 + r); ringA(p, r, 16, 16); }
            rig.block(512);
            expectEquals((int) rig.midiLog.size(), 3);
            bool ordered = rig.midiLog.size() == 3;
            for (size_t i = 0; i < rig.midiLog.size(); ++i)
                ordered = ordered && rig.midiLog[i].isOn() && rig.midiLog[i].data1 == 60 + 2 * (int) i && rig.midiLog[i].frame == 0;
            expect(ordered, "note-ons 60, 62, 64 on channels 1, 2, 3, all at frame 0, in rhythm order");
        }

        beginTest("A retrigger inside the 20 ms hold sends the old note-off immediately before the new note-on at the same frame");
        {
            Rig rig(512);
            auto& p = rig.p;
            midiMode(p, 0, 60);
            ringA(p, 0, 16, 16);
            rig.host.bpm = 4000.0;   // a 16th is 180 frames: every block of 64 frames can hold a new step
            for (int b = 0; b < 8; ++b) rig.block(64);
            std::vector<MidiEv> seq;
            for (const auto& e : rig.midiLog) seq.push_back(e);
            bool found = false;
            for (size_t i = 1; i < seq.size(); ++i)
                if (seq[i].isOn() && seq[i - 1].isOff() && seq[i].frame == seq[i - 1].frame) found = true;
            logMessage("MIDI order: " + juce::String((int) seq.size()) + " events in 8 blocks of 64 frames at 4000 bpm");
            expect(found, "off then on at the same frame");
        }

        beginTest("A removed slot's owed note-off leads the block: it precedes the note-ons of that block");
        {
            Rig rig;
            auto& p = rig.p;
            useRhythms(p, 2);
            midiMode(p, 0, 60);
            ringA(p, 0, 16, 16);
            midiMode(p, 1, 62);
            ringA(p, 1, 16, 16);
            rig.block(1);                      // both rhythms' note-ons, the second one's note-off now owed
            p.removeLayer(1);
            rig.host.ppq += 0.25;              // the host jumps on to the next 16th, so the next block fires rhythm 0 again
            rig.block(64);
            std::vector<MidiEv> second;
            for (size_t i = 2; i < rig.midiLog.size(); ++i) second.push_back(rig.midiLog[i]);
            logMessage("MIDI order after removeLayer: " + juce::String((int) second.size()) + " events in the second block");
            expect(second.size() >= 2 && second[0].isOff() && second[0].data1 == 62, "the orphan note-off 62 is first");
            expect(second.size() >= 2 && second.back().isOn() && second.back().data1 == 60, "then rhythm 0's note-on 60");
        }
    }
};

static MuClidExtractionPinTests muClidExtractionPinTests;

//==============================================================================
// C3 (design 2.1.4) for mu-Clid's EngineTail: a hot-swap's retired engine reads nothing but its own engine. Two
// identical processors retire the same ringing engine (a Kick through a full-feedback Karplus, swapped for a silent
// rhythm while playing); then on ONE every parameter of the processor is scrambled. The slot's channel, which after
// the swap holds only the tail (the new rhythm has no sample), must be bit-identical in both.
struct MuClidRetireIsolationTests : juce::UnitTest
{
    MuClidRetireIsolationTests() : juce::UnitTest("mu-Clid retire tail isolation (C3 twin)", "mu-clid-pins") {}

    static juce::File silentPreset(mu_clid::PluginProcessor& p) { return p.getRhythmsDir().getChildFile("silent-twin.muRhythm"); }

    void runTest() override
    {
        beginTest("A retired engine's tail is the same whatever every parameter does meanwhile");
        mu_contract_test::RetireIsolationCase c;
        c.make = [] { return std::unique_ptr<ProcessorBase>(new mu_golden_rig::OwnedProcessor()); };
        c.setup = [](ProcessorBase& pb)
        {
            auto& p = static_cast<mu_clid::PluginProcessor&>(pb);
            mu_golden_rig::writeFixtureSamples(p);
            ringA(p, 0, 4, 0);
            p.saveRhythmPresetToFile(0, silentPreset(p));   // a silent 4-step rhythm, no sample
            useSample(p, 0, "Kick 01.wav");
            ringA(p, 0, 4, 1);
            karplus(p, 0);
        };
        c.retire = [](ProcessorBase& pb)
        {
            auto& p = static_cast<mu_clid::PluginProcessor&>(pb);
            p.applyRhythmPreset(silentPreset(p), 0);   // installs now: the ringing engine retires into the bin
        };
        c.slot = 0;
        c.blocksBefore = 12;
        c.blocksAfter = 40;
        const auto r = mu_contract_test::checkRetireIsolation(c);
        expect(r.tailFrames == 40 * 256, "the slot's channel kept rendering after the swap");
        expect(r.sounded, "the tail is audible");
        expect(r.ok, "the scrambled twin's tail is bit-identical (the tail reads only its engine): " + r.message);
    }
};

static MuClidRetireIsolationTests muClidRetireIsolationTests;
