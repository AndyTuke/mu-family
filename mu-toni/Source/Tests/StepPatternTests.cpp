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
            expectEquals(a.stepMask, 0b0101u);         // the setting itself never changed
            expectEquals(a.at(2), 1.0f);               // the hidden accent is back
        }

        beginTest("all-On default plays bit-identically to the accent-only pattern");
        {
            ArpAccent oldStyle; oldStyle.pattern = 0b0101; oldStyle.length = 4; oldStyle.amount = 0.8f;   // stepMask untouched
            ArpAccent explicitOn = oldStyle; explicitOn.stepMask = 0xFFFFu;
            expectEquals(oldStyle.stepMask, 0xFFFFu);
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
