// Self-test of the golden-render harness (GoldenRender.h) on a small fake processor and scenario built
// here, so it depends on no product. It proves the harness is repeatable, records and re-checks a
// golden, detects and reports a one-sample change, isolates stems through the mixer solo, enforces the
// tolerance rule, and calls the allocation-counter hook.

#include "GoldenRender.h"
#include "TestProcessorBase.h"

namespace
{
    // Two layers (a sine and a ppq-gated saw) mixed through the shared strip parameters, with solo and
    // sends honoured, so the harness's stem mechanism has something real to isolate.
    struct FakeSynth : TestProcessorBase
    {
        static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
        {
            juce::AudioProcessorValueTreeState::ParameterLayout layout;
            for (int i = 0; i < 2; ++i)
            {
                const auto p = "ch" + juce::String(i) + "_";
                layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { p + "lvl", 1 }, "lvl", 0.0f, 1.0f, 0.8f));
                layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { p + "solo", 1 }, "solo", false));
                for (const char* s : { "sendEff", "sendDly", "sendRev" })
                    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { p + s, 1 }, s, 0.0f, 1.0f, 0.25f));
            }
            return layout;
        }

        FakeSynth() : TestProcessorBase(WrapperType::wrapperType_Undefined, makeLayout()) {}

        // Test knob: add a tiny offset to the left channel at one absolute frame (a "one-sample change").
        juce::int64 perturbFrame = -1;

        void prepareToPlay(double sr, int) override { rate = sr; frame = 0; phase[0] = phase[1] = 0.0; freq = 220.0; }

        void processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) override
        {
            // Latest note-on sets the pitch; sample-accurate handling is not needed for a fake.
            for (const auto meta : midi)
                if (meta.getMessage().isNoteOn())
                    freq = juce::MidiMessage::getMidiNoteInHertz(meta.getMessage().getNoteNumber());

            double ppq = 0.0;
            if (auto* head = getPlayHead())
                if (auto pos = head->getPosition())
                    ppq = pos->getPpqPosition().orFallback(0.0);

            bool anySolo = false;
            for (int k = 0; k < 2; ++k) anySolo = anySolo || param(k, "solo") > 0.5f;

            const int n = buf.getNumSamples();
            for (int i = 0; i < n; ++i)
            {
                float mix = 0.0f;
                for (int k = 0; k < 2; ++k)
                {
                    const double f = k == 0 ? freq : freq * 2.0;
                    phase[k] += f / rate;
                    phase[k] -= std::floor(phase[k]);
                    const float raw = k == 0 ? (float) std::sin(phase[k] * juce::MathConstants<double>::twoPi)
                                             : (float) (phase[k] * 2.0 - 1.0) * (0.5f + 0.5f * (float) std::cos(ppq * juce::MathConstants<double>::twoPi));
                    const bool audible = ! anySolo || param(k, "solo") > 0.5f;
                    const float send = param(k, "sendEff") + param(k, "sendDly") + param(k, "sendRev");
                    if (audible) mix += raw * param(k, "lvl") * (1.0f + 0.1f * send);
                }
                float l = mix * 0.5f, r = mix * 0.4f;
                if (perturbFrame >= 0 && frame == perturbFrame) l += 0.001f;
                buf.setSample(0, i, l);
                buf.setSample(1, i, r);
                ++frame;
            }
            mixerEngine.masterPeakL.store(buf.getMagnitude(0, 0, n));
            mixerEngine.masterPeakR.store(buf.getMagnitude(1, 0, n));
        }

        void getStateInformation(juce::MemoryBlock& dest) override
        {
            const juce::String s = juce::String(param(0, "lvl")) + "/" + juce::String(param(1, "lvl"));
            dest.replaceAll(s.toRawUTF8(), s.getNumBytesAsUTF8());
        }

        float param(int layer, const char* id) const
        {
            auto* v = apvts.getRawParameterValue("ch" + juce::String(layer) + "_" + id);
            return v != nullptr ? v->load() : 0.0f;
        }

        double rate = 48000.0, freq = 220.0;
        double phase[2] {};
        juce::int64 frame = 0;
    };

    mu_golden::Scenario makeScenario()
    {
        mu_golden::Scenario sc;
        sc.name = "fake";
        sc.lengthFrames = 6000;                 // 24 segments, a partial one at the end
        sc.numStems = 2;
        sc.inputHash = "fake-inputs-v1";
        sc.loadState = [](ProcessorBase& p)
        {
            if (auto* a = p.apvts.getParameter("ch1_lvl")) a->setValueNotifyingHost(0.6f);
        };
        // Scripted events at block indices: a note on at block 2 (via MIDI), a level change at block 5.
        sc.midi.push_back({ 2, 3, juce::MidiMessage::noteOn(1, 69, (juce::uint8) 100) });
        sc.events.push_back({ 5, "level", [](mu_golden::ScriptContext& c)
        {
            if (auto* a = c.proc.apvts.getParameter("ch0_lvl")) a->setValueNotifyingHost(0.5f);
        } });
        return sc;
    }

    mu_golden::ProcessorFactory makeFactory(juce::int64 perturb = -1)
    {
        return [perturb]
        {
            auto p = std::make_unique<FakeSynth>();
            p->perturbFrame = perturb;
            return std::unique_ptr<ProcessorBase>(std::move(p));
        };
    }

    // Counts calls so the test can see the hook is driven once per processBlock.
    struct CountingHook : mu_golden::AllocationCounter
    {
        int begins = 0, ends = 0;
        void beginBlock() override { ++begins; }
        juce::int64 endBlock() override { ++ends; return 2; }
    };

    struct TempGoldenStore
    {
        TempGoldenStore() : root(juce::File::getSpecialLocation(juce::File::tempDirectory)
                                     .getChildFile("mu-golden-test-" + juce::String::toHexString(juce::Random().nextInt64())))
        {
            store.goldenRoot = root.getChildFile("golden");
            store.dumpRoot = root.getChildFile("dump");
        }
        ~TempGoldenStore() { root.deleteRecursively(); }
        juce::File root;
        mu_golden::GoldenStore store;
    };
}

struct GoldenRenderHarnessTests : juce::UnitTest
{
    GoldenRenderHarnessTests() : juce::UnitTest("GoldenRenderHarness", "mu-core") {}

    void runTest() override
    {
        using namespace mu_golden;

        beginTest("REPEAT: rendering twice in one process gives identical fingerprints");
        {
            TempGoldenStore t;
            const auto out = t.store.verifyRepeatable(makeScenario(), makeFactory());
            expect(out.ok, out.message);
        }

        beginTest("The fingerprint covers master and both stems on both schedules, in 256-frame segments");
        {
            const auto cap = renderScenario(makeFactory(), makeScenario());
            std::map<juce::String, juce::String> m(cap.lines.begin(), cap.lines.end());
            for (const char* sched : { "fixed512", "ragged" })
                for (const char* stream : { "master", "stem0", "stem1" })
                {
                    const juce::String p = juce::String(sched) + "." + stream;
                    expect(m.count(p + ".whole") == 1, p + ".whole present");
                    // 6000 frames = 23 full segments (0..22) plus a 112-frame tail segment (23).
                    expect(m.count(p + ".seg.000000") == 1 && m.count(p + ".seg.000023") == 1, p + " first segment and partial tail segment");
                    expect(m.count(p + ".seg.000024") == 0, p + " no segment past the end");
                    expect(m.count(p + ".state.hash") == 1 && m.count(p + ".end.masterPeak") == 1, p + " end values");
                }
            expect(m["fixed512.master.whole"] != m["fixed512.stem0.whole"], "solo isolates stem0 from the master");
            expect(m["fixed512.stem0.whole"] != m["fixed512.stem1.whole"], "the two stems differ");
            expect(m["fixed512.master.whole"] != m["ragged.master.whole"], "each schedule is its own render (the fake is block-sensitive)");
        }

        beginTest("Record a golden, then verify against it; a missing golden and changed inputs both fail");
        {
            TempGoldenStore t;
            const auto sc = makeScenario();

            auto missing = t.store.verify("fakeproduct", sc, makeFactory());
            expect(! missing.ok && missing.message.contains("no golden"), "missing golden is reported: " + missing.message);

            auto rec = t.store;
            rec.updateMode = true;
            rec.reason = "self-test";
            const auto noFilter = rec.verify("fakeproduct", sc, makeFactory());
            expect(! noFilter.ok, "update without a scenario filter is refused (there is no update-all)");
            rec.scenarioFilter = "fakeproduct/other";
            expect(rec.verify("fakeproduct", sc, makeFactory()).skipped, "a different scenario is skipped");
            rec.scenarioFilter = "fakeproduct/fake";
            const auto done = rec.verify("fakeproduct", sc, makeFactory());
            expect(done.ok, done.message);
            expect(rec.goldenFile("fakeproduct", sc).existsAsFile(), "golden file written under <product>/<scenario>/golden.<config>.txt");
            expect(rec.goldenFile("fakeproduct", sc).getFileName() == "golden." + currentConfig() + ".txt", "file is per config");
            expect(rec.goldenFile("fakeproduct", sc).loadFileAsString().contains("meta.reason=self-test"), "reason recorded in the file");

            const auto same = t.store.verify("fakeproduct", sc, makeFactory());
            expect(same.ok, same.message);

            auto changed = sc;
            changed.inputHash = "fake-inputs-v2";
            const auto stale = t.store.verify("fakeproduct", changed, makeFactory());
            expect(! stale.ok && stale.message.contains("inputs changed"), "changed scenario inputs are refused: " + stale.message);

            auto noReason = t.store;
            noReason.updateMode = true;
            noReason.scenarioFilter = "fakeproduct/fake";
            expect(! noReason.verify("fakeproduct", sc, makeFactory()).ok, "update without a reason is refused");
        }

        beginTest("A one-sample change is detected and reported with its segment, with a dump written");
        {
            TempGoldenStore t;
            const auto sc = makeScenario();
            auto rec = t.store;
            rec.updateMode = true;
            rec.reason = "self-test";
            rec.scenarioFilter = "fakeproduct/fake";
            expect(rec.verify("fakeproduct", sc, makeFactory()).ok, "recorded");

            // Frame 5000 is in segment 19 (5000 / 256).
            const auto bad = t.store.verify("fakeproduct", sc, makeFactory(5000));
            expect(! bad.ok, "one changed sample must fail");
            expect(bad.message.contains("fixed512.master.seg.000019"), "names the first diverging segment: " + bad.message);
            expect(! bad.message.contains("seg.000018"), "does not blame an earlier segment");

            const auto dump = t.store.dumpRoot.getChildFile("fakeproduct").getChildFile("fake");
            expect(dump.getChildFile(currentConfig() + ".diff.txt").existsAsFile(), "diff list dumped");
            expect(dump.getChildFile(currentConfig() + ".actual.txt").existsAsFile(), "actual fingerprint dumped");
            expect(dump.getChildFile(currentConfig() + ".golden.txt").existsAsFile(), "golden copy dumped");
            expect(dump.getChildFile(currentConfig() + ".fixed512.master.actual.wav").existsAsFile(), "actual render dumped as WAV");

            // The same change on an earlier segment boundary moves the reported segment with it.
            const auto bad2 = t.store.verify("fakeproduct", sc, makeFactory(256));
            expect(bad2.message.contains("fixed512.master.seg.000001"), "segment follows the changed sample: " + bad2.message);
        }

        beginTest("Tolerance needs a written reason; a non-zero tolerance with a reason is accepted for equal renders");
        {
            TempGoldenStore t;
            auto sc = makeScenario();
            auto rec = t.store;
            rec.updateMode = true;
            rec.reason = "self-test";
            rec.scenarioFilter = "fakeproduct/fake";
            expect(rec.verify("fakeproduct", sc, makeFactory()).ok, "recorded");

            sc.toleranceAbs = 4.0;
            const auto noWhy = t.store.verify("fakeproduct", sc, makeFactory());
            expect(! noWhy.ok && noWhy.message.contains("toleranceReason"), "tolerance without a reason is refused: " + noWhy.message);
            sc.toleranceReason = "self-test: proves the field is honoured";
            expect(t.store.verify("fakeproduct", sc, makeFactory()).ok, "tolerance with a reason compares peaks and passes");
        }

        beginTest("The MIDI-out hash and the state hash move when MIDI output or saved state change");
        {
            auto a = makeScenario();
            auto b = makeScenario();
            const auto capA = renderScenario(makeFactory(), a);
            b.loadState = [](ProcessorBase& p)
            {
                if (auto* x = p.apvts.getParameter("ch1_lvl")) x->setValueNotifyingHost(0.3f);
            };
            const auto capB = renderScenario(makeFactory(), b);
            std::map<juce::String, juce::String> ma(capA.lines.begin(), capA.lines.end()), mb(capB.lines.begin(), capB.lines.end());
            expect(ma["fixed512.master.state.hash"] != mb["fixed512.master.state.hash"], "saved-state hash changes with state");
            expect(ma["fixed512.master.midi.hash"] == mb["fixed512.master.midi.hash"], "midi hash is stable for the same MIDI out");
        }

        beginTest("The allocation-counter hook is called around every processBlock and its total is reported");
        {
            CountingHook hook;
            const auto cap = renderScenario(makeFactory(), makeScenario(), &hook);
            expect(hook.begins > 0 && hook.begins == hook.ends, "begin/end paired: " + juce::String(hook.begins));
            expectEquals<juce::int64>(cap.allocations, (juce::int64) hook.ends * 2);
            const auto none = renderScenario(makeFactory(), makeScenario(), nullptr);
            expectEquals<juce::int64>(none.allocations, -1);
        }
    }
};

static GoldenRenderHarnessTests goldenRenderHarnessTests;
