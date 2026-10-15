// The Off / On / Accented step pattern: an Off step is a rest (no note, but the scan and the clock move on),
// a Legato tie ends at a rest and the next note is a fresh pluck, an accent bit on an Off step is ignored,
// and the default (every step On) plays exactly as the accent-only pattern did.

#include "Plugin/PluginProcessor.h"
#include "Sequencer/ArpVoiceRunner.h"
#include "Tests/ProcessorSeamHelpers.h"

using namespace mu_toni;

namespace
{
constexpr double kSr    = 48000.0;
constexpr int    kBlock = 512;
constexpr double kBps   = (120.0 / 60.0) / kSr;
constexpr double kStepSamples = 0.25 / kBps;   // 6000 samples per 1/16
constexpr int    kRate16th = 6;

ArpContext playingAt(double startBeat)
{
    ArpContext c;
    c.playing = true; c.sampleRate = kSr; c.bpm = 120.0;
    c.startBeat = startBeat; c.beatsPerSample = kBps;
    return c;
}

// A fresh runner on the factory bank with a short amp release, so a released note is gone (isActive false) quickly.
struct Rig
{
    mu_wavetable::WavetableBank bank;
    ArpVoiceRunner runner;
    juce::AudioBuffer<float> buf { 2, kBlock };
    double beat = 0.0;
    long long sample = 0;

    Rig(const ArpAccent& accent, bool legato, float gate)
    {
        bank.loadFactoryBank();
        ToniVoiceParams tv;
        tv.ampR = 0.001f; tv.ampS = 1.0f; tv.legato = legato; tv.portamentoMs = 200.0f;
        runner.setBank(&bank);
        runner.prepare(kSr, kBlock);
        runner.setVoiceParams(tv);
        runner.setAccent(accent);
        runner.setStep(kRate16th, gate, false);
    }

    // Renders one block; returns its peak.
    float block()
    {
        runner.render(buf, kBlock, playingAt(beat));
        beat += kBps * kBlock;
        sample += kBlock;
        return buf.getMagnitude(0, 0, kBlock);
    }
    // Renders whole blocks until `sample` reaches `untilSample`.
    void runTo(long long untilSample) { while (sample < untilSample) block(); }
};
}

struct StepPatternTests : juce::UnitTest
{
    StepPatternTests() : juce::UnitTest("mu-Toni step pattern (Off / On / Accented)", "mu-toni") {}

    void runTest() override
    {
        beginTest("cells: Off is a rest, On plays, Accented lifts; an accent bit on an Off step is ignored");
        {
            ArpAccent a; a.length = 4; a.amount = 0.8f;
            a.stepMask = 0b1011;   // step 2 Off
            a.pattern  = 0b1110;   // steps 1 and 3 Accented; step 2's accent bit is set but its cell is Off
            expect(a.isOn(0));  expect(a.isOn(1));  expect(! a.isOn(2));  expect(a.isOn(3));
            expectEquals(a.at(0), 0.0f);  expectEquals(a.at(1), 0.8f);
            expectEquals(a.at(2), 0.0f);  expectEquals(a.at(3), 0.8f);
            expect(! a.isOn(6));   // the cell repeats every `length` steps
        }

        beginTest("bits at or above Steps are kept but ignored; raising Steps brings them back");
        {
            ArpAccent a; a.amount = 1.0f;
            a.stepMask = 0b0101; a.pattern = 0b0100;   // cell 0 On, 1 Off, 2 Accented, 3 Off
            a.length = 2;
            expect(a.isOn(0)); expect(! a.isOn(1)); expect(a.isOn(2)); expectEquals(a.at(2), 0.0f);   // step 2 is cell 0 again
            a.length = 4;
            expectEquals((int) a.stepMask, 0b0101);         // the setting itself never changed
            expectEquals(a.at(2), 1.0f);               // the hidden accent is back
        }

        beginTest("all-On default plays bit-identically to the accent-only pattern");
        {
            ArpAccent oldStyle; oldStyle.pattern = 0b0101; oldStyle.length = 4; oldStyle.amount = 0.8f;   // stepMask untouched
            ArpAccent explicitOn = oldStyle; explicitOn.stepMask = 0xFFFFu;
            expectEquals((int) oldStyle.stepMask, 0xFFFF);
            Rig a(oldStyle, false, 0.5f), b(explicitOn, false, 0.5f);
            bool same = true;
            for (int i = 0; i < 100; ++i)
            {
                a.block(); b.block();
                for (int ch = 0; ch < 2; ++ch)
                    same = same && std::memcmp(a.buf.getReadPointer(ch), b.buf.getReadPointer(ch), sizeof(float) * kBlock) == 0;
            }
            expect(same);
            expectGreaterThan(a.runner.stepsFired(), 8);
        }

        beginTest("a rest triggers no note but the step counter advances");
        {
            ArpAccent a; a.length = 2; a.stepMask = 0b10;   // step 0 Off, step 1 On
            Rig r(a, false, 0.5f);
            r.runTo((long long) (kStepSamples * 0.9));   // inside step 0
            expectEquals(r.runner.stepsFired(), 1);
            expectEquals(r.runner.lastStep(), 0);
            expect(! r.runner.isActive());               // no note-on
            r.runTo((long long) (kStepSamples * 1.2));
            expectEquals(r.runner.lastStep(), 1);        // the scan moved on
            expect(r.runner.isActive());                 // and step 1 sounds
        }

        beginTest("a rest is bar-locked: the same steps fire as with every step On");
        {
            ArpAccent allOn; allOn.length = 4;
            ArpAccent holes = allOn; holes.stepMask = 0b0101;
            Rig a(allOn, false, 0.5f), b(holes, false, 0.5f);
            for (int i = 0; i < 100; ++i) { a.block(); b.block(); }
            expectEquals(a.runner.stepsFired(), b.runner.stepsFired());
            expectEquals(a.runner.lastStep(), b.runner.lastStep());
        }

        beginTest("an accent bit on an Off step changes nothing in the output");
        {
            ArpAccent clean; clean.length = 4; clean.amount = 1.0f; clean.stepMask = 0b1011; clean.pattern = 0b0011;
            ArpAccent dirty = clean; dirty.pattern |= 0b0100;   // accent bit on the Off step 2
            Rig a(clean, false, 0.5f), b(dirty, false, 0.5f);
            bool same = true;
            for (int i = 0; i < 100; ++i)
            {
                a.block(); b.block();
                same = same && std::memcmp(a.buf.getReadPointer(0), b.buf.getReadPointer(0), sizeof(float) * kBlock) == 0;
            }
            expect(same);
        }

        beginTest("Legato: a rest ends the tie at the step boundary, and the next note is a fresh pluck (no glide)");
        {
            ArpAccent a; a.length = 3; a.stepMask = 0b101;   // step 1 Off
            ArpAccent control = a; control.stepMask = 0xFFFFu;
            Rig r(a, true, 1.0f), c(control, true, 1.0f);   // 100 % gate + Legato = tie
            // Part-way into step 1 the control's tie is still sounding; the rest has already released it.
            r.runTo((long long) kStepSamples + 3000); c.runTo((long long) kStepSamples + 3000);
            expect(! r.runner.isActive());
            expect(c.runner.isActive());
            // Step 2 after the rest sounds: a legato glide note would not retrigger the released envelope.
            float peak = 0.0f;
            r.runTo((long long) (2 * kStepSamples) + kBlock);
            while (r.sample < (long long) (3 * kStepSamples) - kBlock) peak = juce::jmax(peak, r.block());
            expectGreaterThan(peak, 0.01f);
        }

        beginTest("a rest cuts a held note whose gate outlived a tempo change (60 -> 240 BPM mid-gate)");
        {
            ArpAccent a; a.length = 2; a.stepMask = 0b01;   // step 0 On, step 1 Off
            Rig r(a, false, 0.9f);
            auto ctxAt = [&](double bpm) { auto c = playingAt(r.beat); c.bpm = bpm; c.beatsPerSample = bpm / 60.0 / kSr; return c; };
            auto renderAt = [&](double bpm)
            {
                r.runner.render(r.buf, kBlock, ctxAt(bpm));
                r.beat += bpm / 60.0 / kSr * kBlock;
            };
            renderAt(60.0);   // step 0 fires at 60 BPM: a 0.9 gate of 12000 samples
            expect(r.runner.isActive());
            // At 240 BPM the step-1 boundary (beat 0.25) comes ~2900 samples on, well inside that gate.
            int blocks = 0;
            while (r.beat + 240.0 / 60.0 / kSr * kBlock < 0.25 && blocks++ < 100) renderAt(240.0);
            expect(r.runner.isActive());   // still inside the old gate, before the boundary block
            renderAt(240.0); renderAt(240.0);   // the boundary block fires the rest, the next lets the release run out
            expectEquals(r.runner.lastStep(), 1);
            expect(! r.runner.isActive());   // the rest cut the note: gone within its release
        }

        beginTest("stop then start with Legato ties: step 0 is a fresh pluck, not a glide from a silent voice");
        {
            ArpAccent a; a.length = 4;
            Rig r(a, true, 1.0f);
            r.runTo((long long) (1.5 * kStepSamples));   // a tie is held and primed to glide
            expect(r.runner.isActive());
            auto stopped = playingAt(r.beat); stopped.playing = false;
            for (int i = 0; i < 4; ++i) r.runner.render(r.buf, kBlock, stopped);
            expect(! r.runner.isActive());
            r.beat = 0.0;   // transport restarts from the top
            float peak = 0.0f;
            for (int i = 0; i < 4; ++i) peak = juce::jmax(peak, r.block());
            expectGreaterThan(peak, 0.01f);
        }

        beginTest("MIDI mode: a rest at step 0 on a fresh key press releases the held tie, nothing hangs");
        {
            ArpAccent a; a.length = 2; a.stepMask = 0b10;   // step 0 Off, step 1 On
            Rig r(a, true, 1.0f);
            r.runner.setStep(kRate16th, 1.0f, true);
            auto held = [&](bool edge) { auto c = playingAt(r.beat); c.anyNoteHeld = true; c.noteOnEdge = edge; return c; };
            r.runner.render(r.buf, kBlock, held(true));   // key press: step 0 rests
            while (r.sample < (long long) (kStepSamples * 1.5))
            { r.runner.render(r.buf, kBlock, held(false)); r.sample += kBlock; }
            expect(r.runner.isActive());                  // step 1 is tied and sounding
            r.runner.render(r.buf, kBlock, held(true));   // a second key press restarts at the resting step 0
            r.runner.render(r.buf, kBlock, held(false));
            expect(! r.runner.isActive());
        }

        beginTest("Legato with a rest as the very first step: silent, then a fresh pluck, then a glide");
        {
            ArpAccent a; a.length = 3; a.stepMask = 0b110;   // step 0 Off
            Rig r(a, true, 1.0f);
            r.runTo((long long) (kStepSamples * 0.5));
            expect(! r.runner.isActive());
            float peak = 0.0f;
            r.runTo((long long) kStepSamples);
            while (r.sample < (long long) (kStepSamples * 1.6)) peak = juce::jmax(peak, r.block());
            expectGreaterThan(peak, 0.01f);
            r.runTo((long long) (kStepSamples * 2.5));
            expect(r.runner.isActive());   // the tie carries into step 2
        }

        beginTest("block size does not change which steps fire or which sound (1, 64, 512, 4096)");
        {
            ArpAccent a; a.length = 4; a.stepMask = 0b1101; a.pattern = 0b1000; a.amount = 0.7f;
            std::vector<std::vector<int>> sounding;
            for (int bs : { 1, 64, 512, 4096 })
            {
                mu_wavetable::WavetableBank bank; bank.loadFactoryBank();
                ArpVoiceRunner arpRunner; ToniVoiceParams tv; tv.ampR = 0.001f;
                arpRunner.setBank(&bank); arpRunner.prepare(kSr, bs);
                arpRunner.setVoiceParams(tv); arpRunner.setAccent(a); arpRunner.setStep(kRate16th, 0.5f, false);
                juce::AudioBuffer<float> buf(2, bs);
                std::vector<int> seq;
                int fired = 0;
                for (long long s = 0; s < 60000; s += bs)
                {
                    arpRunner.render(buf, bs, playingAt(kBps * (double) s));
                    if (arpRunner.stepsFired() != fired)
                    {
                        fired = arpRunner.stepsFired();
                        if (a.isOn(arpRunner.lastStep())) seq.push_back(arpRunner.lastStep());   // only the steps that sound
                    }
                }
                seq.resize(juce::jmin((size_t) 7, seq.size()));   // steps 0..9 all fire before 60000 in every size
                sounding.push_back(seq);
            }
            for (size_t i = 1; i < sounding.size(); ++i) expect(sounding[i] == sounding[0]);
            expectEquals((int) sounding[0].size(), 7);
        }

        beginTest("changing Steps mid-pattern ignores the hidden cells, and raising it brings them back");
        {
            ArpAccent a; a.length = 4; a.stepMask = 0b0101; a.pattern = 0b0100; a.amount = 1.0f;   // 0 On, 1 Off, 2 Accented, 3 Off
            Rig r(a, false, 0.5f);
            r.runTo((long long) (kStepSamples * 3.0) + 1500);
            auto shorter = a; shorter.length = 2;   // cells 0 and 1 only
            r.runner.setAccent(shorter);
            expectEquals((int) shorter.stepMask, 0b0101);   // the hidden cells are untouched
            r.runTo((long long) (kStepSamples * 4.0) + 1500);
            expect(r.runner.isActive());               // step 4 = cell 0, On
            r.runTo((long long) (kStepSamples * 5.0) + 1500);
            expect(! r.runner.isActive());             // step 5 = cell 1, Off
            r.runner.setAccent(a);                     // Steps back to 4
            r.runTo((long long) (kStepSamples * 6.0) + 1500);
            expect(r.runner.isActive());               // step 6 = cell 2, On again
            r.runTo((long long) (kStepSamples * 7.0) + 1500);
            expect(! r.runner.isActive());             // step 7 = cell 3, Off
        }

        beginTest("a layer without stepMask loads with every step On; a saved value survives a state round trip");
        {
            mu_seam_test::TempStorage storage;
            PluginProcessor proc;
            expectWithinAbsoluteError(mu_seam_test::getParam(proc.apvts, "a0_stepMask"), 65535.0f, 0.5f);
            mu_seam_test::setParam(proc.apvts, "a0_stepMask", 5.0f);

            const char* oldDump = R"(<MuToniState><PARAM id="a0_accPat" value="0.0"/></MuToniState>)";
            juce::MemoryBlock block;
            juce::AudioProcessor::copyXmlToBinary(*juce::XmlDocument::parse(oldDump), block);
            PluginProcessor fresh;
            fresh.setStateInformation(block.getData(), (int) block.getSize());
            expectWithinAbsoluteError(mu_seam_test::getParam(fresh.apvts, "a0_stepMask"), 65535.0f, 0.5f);

            juce::MemoryBlock saved;
            proc.getStateInformation(saved);
            PluginProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectWithinAbsoluteError(mu_seam_test::getParam(loaded.apvts, "a0_stepMask"), 5.0f, 0.5f);
        }
    }
};

static StepPatternTests stepPatternTests;
