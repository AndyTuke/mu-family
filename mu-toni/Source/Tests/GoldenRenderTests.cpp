// Layer-extraction safety net for mu-Toni: golden-render scenarios and the behaviour pins, both on the
// REAL headless PluginProcessor.
//
// Part 1, goldens. Each scenario is a frozen state (tests/golden/mu-toni/<name>/state.xml) plus a
// description (scenario.json) and the scripted events / MIDI below, rendered through
// mu-core/Tests/GoldenRender.h and compared bit for bit with tests/golden/mu-toni/<name>/golden.<config>.txt.
// The goldens change only through tests/scripts/update-golden.ps1.
//
// Part 2, pins. Unit tests that assert what the code does TODAY where a later extraction stage is
// expected to move it (the modulation resolve into voice parameters, the held-note stack, root-by-MIDI,
// MIDI-trigger mode, a structural edit mid-play, a block above the prepared size). They are not goldens
// and not wishes: when a stage changes one on purpose, the stage updates the number here in the same
// commit and says why.

// This translation unit carries the allocation probe's global operator new / delete (test executable only).
#define MU_ALLOCATION_PROBE_DEFINE_OPERATORS
#include "Plugin/PluginProcessor.h"
#include "Tests/GoldenAllocationCheck.h"
#include "Tests/GoldenRender.h"
#include "Tests/ProcessorSeamHelpers.h"

#include <cmath>
#include <cstring>
#include <functional>
#include <memory>

namespace
{
    // Blocks that allocate or free inside processBlock today, recorded with their exact counts (see
    // GoldenAllocationCheck.h). Informational: reported on every run, never an approval to allocate.
    const std::vector<mu_golden::KnownAllocation> kKnownAllocations = {
    };

    using mu_golden::ScriptContext;

    constexpr const char* kProduct = "mu-toni";

    // The repo root from this source file (<root>/mu-toni/Source/Tests/GoldenRenderTests.cpp), so the
    // scenario inputs are found even when MU_GOLDEN_ROOT points the golden files at a scratch folder.
    juce::File repoRoot()
    {
        return juce::File(__FILE__).getParentDirectory().getParentDirectory().getParentDirectory().getParentDirectory();
    }

    // ─── Part 1: scenarios ─────────────────────────────────────────────────────

    juce::MidiMessage on(int ch, int note)  { return juce::MidiMessage::noteOn(ch, note, (juce::uint8) 100); }
    juce::MidiMessage off(int ch, int note) { return juce::MidiMessage::noteOff(ch, note, (juce::uint8) 0); }

    void tempoAt(mu_golden::Scenario& sc, int block, double bpm)
    {
        sc.events.push_back({ block, "tempo " + juce::String(bpm), [bpm](ScriptContext& c) { c.host.bpm = bpm; } });
    }
    void stopAt(mu_golden::Scenario& sc, int block, bool resetBeat)
    {
        sc.events.push_back({ block, "stop", [resetBeat](ScriptContext& c)
        {
            c.host.playing = false;
            if (resetBeat) c.host.ppq = 0.0;
        } });
    }
    void startAt(mu_golden::Scenario& sc, int block)
    {
        sc.events.push_back({ block, "start", [](ScriptContext& c) { c.host.playing = true; } });
    }

    struct ScenarioDef
    {
        const char* name;
        std::function<void(mu_golden::Scenario&)> script;   // the scripted events and MIDI for this scenario
    };

    const std::vector<ScenarioDef>& scenarioDefs()
    {
        static const std::vector<ScenarioDef> defs {
            { "loop-accent-steps", [](mu_golden::Scenario&) {} },
            { "loop-legato-rests", [](mu_golden::Scenario&) {} },

            { "midi-trigger", [](mu_golden::Scenario& sc)
              {
                  sc.midi = { { 3, 100, on(1, 60) },  { 10, 255, on(1, 64) }, { 30, 0, off(1, 64) },
                              { 45, 17, off(1, 60) }, { 60, 0, on(1, 67) },   { 80, 7, off(1, 67) } };
              } },

            { "root-by-midi", [](mu_golden::Scenario& sc)
              {
                  sc.midi = { { 2, 0, on(1, 50) },  { 5, 0, on(2, 62) },   { 25, 300, on(2, 67) }, { 40, 0, off(2, 62) },
                              { 55, 0, off(2, 67) }, { 70, 0, on(2, 69) },  { 72, 0, on(2, 71) },
                              { 85, 0, juce::MidiMessage::allNotesOff(2) } };
              } },

            { "tempo-change", [](mu_golden::Scenario& sc)
              {
                  tempoAt(sc, 30, 90.0);  tempoAt(sc, 60, 150.0);  tempoAt(sc, 90, 133.3);
              } },

            { "transport-stop-start", [](mu_golden::Scenario& sc)
              {
                  stopAt(sc, 25, false);  startAt(sc, 40);
                  sc.events.push_back({ 70, "locate 17.3", [](ScriptContext& c) { c.host.ppq = 17.3; } });
                  stopAt(sc, 100, true);  startAt(sc, 104);
              } },

            { "two-layers", [](mu_golden::Scenario& sc)
              {
                  sc.events.push_back({ 80, "add layer", [](ScriptContext& c) { c.proc.addLayer(); } });
              } },

            { "modulation",       [](mu_golden::Scenario&) {} },
            { "insert-effect",    [](mu_golden::Scenario&) {} },
            { "noise-on",         [](mu_golden::Scenario&) {} },
            { "master-fx-return", [](mu_golden::Scenario&) {} },
        };
        return defs;
    }

    // The scenario from its frozen files plus the scripted part above. inputHash covers both files, so an
    // edited state or description cannot pass against an old golden.
    mu_golden::Scenario buildScenario(const ScenarioDef& def)
    {
        const auto dir = repoRoot().getChildFile("tests/golden").getChildFile(kProduct).getChildFile(def.name);
        const auto stateFile = dir.getChildFile("state.xml");
        const auto jsonFile  = dir.getChildFile("scenario.json");

        const auto json = juce::JSON::parse(jsonFile.loadFileAsString());
        mu_golden::Scenario sc;
        sc.name              = def.name;
        sc.sampleRate        = (double) json["sampleRate"];
        sc.preparedBlockSize = (int) json["preparedBlockSize"];
        sc.lengthFrames      = (juce::int64) json["lengthFrames"];
        sc.tempoBpm          = (double) json["tempoBpm"];
        sc.startBeat         = (double) json["startBeat"];
        sc.numStems          = (int) json["numStems"];
        sc.inputHash         = mu_golden::hashFiles({ stateFile, jsonFile });

        // The frozen state goes in as a host session (the same path a DAW restore takes).
        const auto stateText = stateFile.loadFileAsString();
        sc.loadState = [stateText](ProcessorBase& p)
        {
            const auto xml = juce::XmlDocument::parse(stateText);
            juce::MemoryBlock mb;
            juce::AudioProcessor::copyXmlToBinary(*xml, mb);
            p.setStateInformation(mb.getData(), (int) mb.getSize());
        };

        def.script(sc);
        return sc;
    }

    mu_golden::ProcessorFactory productFactory()
    {
        return [] { return std::unique_ptr<ProcessorBase>(new mu_toni::PluginProcessor()); };
    }

    // ─── Part 2 helpers ────────────────────────────────────────────────────────

    // The real processor driven block by block with a playhead the test owns; 48 kHz, 120 bpm, playing.
    struct Rig
    {
        explicit Rig(int preparedBlock = 512)
        {
            host.bpm = 120.0;
            p.setPlayHead(&host);
            p.setRateAndBufferSizeDetails(48000.0, preparedBlock);
            p.prepareToPlay(48000.0, preparedBlock);
        }
        ~Rig() { p.setPlayHead(nullptr); p.releaseResources(); }

        struct Result { float peak = 0.0f; mu_golden::Fnv64 hash; };

        Result block(int n = 512, juce::MidiBuffer midi = {})
        {
            host.timeInSamples = frame;
            juce::AudioBuffer<float> view(work.getArrayOfWritePointers(), 2, n);
            view.clear();
            p.processBlock(view, midi);
            Result r;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                {
                    const float v = view.getSample(c, i);
                    r.hash.f32(v);
                    r.peak = std::max(r.peak, std::abs(v));
                }
            if (host.playing) host.ppq += (double) n * host.bpm / (60.0 * 48000.0);
            frame += n;
            return r;
        }

        void set(const juce::String& id, float plain) { mu_seam_test::setParam(p.apvts, id, plain); }
        auto& runner(int layer) { return p.getArp(layer).runner; }

        mu_toni::PluginProcessor p;
        mu_golden::MockHost host;
        juce::AudioBuffer<float> work { 2, 16384 };
        juce::int64 frame = 0;
    };

    void loadStateFile(mu_toni::PluginProcessor& p, const juce::File& file)
    {
        const auto xml = juce::XmlDocument::parse(file);
        juce::MemoryBlock mb;
        juce::AudioProcessor::copyXmlToBinary(*xml, mb);
        p.setStateInformation(mb.getData(), (int) mb.getSize());
    }
}

//==============================================================================
struct MuToniGoldenRenderTests : juce::UnitTest
{
    MuToniGoldenRenderTests() : juce::UnitTest("mu-Toni golden renders", "mu-toni-golden") {}

    void runTest() override
    {
        mu_seam_test::TempStorage storage;                                      // no real per-user folder is read or written
        mu_seam_test::ScopedEnvVar deterministic("MU_DETERMINISTIC_NOISE", "1");   // noise repeats run to run (set before any processor is built)
        beginTest("allocation probe sees heap use and ignores debug-STL proxies");
        for (const auto& f : mu_golden::probeSelfTest()) expect(false, f);

        auto store = mu_golden::GoldenStore::fromEnvironment();
        mu_golden::AllocationProbeCounter allocProbe;
        store.allocationCounter = &allocProbe;

        for (const auto& def : scenarioDefs())
        {
            beginTest(def.name);

            // Record mode renders one named scenario only; the others are skipped without rendering.
            if (store.updateMode && store.scenarioFilter != juce::String(kProduct) + "/" + def.name) continue;

            const auto sc = buildScenario(def);

            // Twice in this process must match; update-golden.ps1 adds the cross-process pair when recording,
            // so a recording run does not spend two more renders on it.
            if (! store.updateMode)
            {
                const auto repeat = store.verifyRepeatable(sc, productFactory());
                expect(repeat.ok, repeat.message);
            }

            allocProbe.setScenario(def.name);
            const auto result = store.verify(kProduct, sc, productFactory());
            if (! result.skipped) expect(result.ok, result.message);
            if (result.message.isNotEmpty()) logMessage(result.message);

            // Heap activity inside processBlock: anything not listed in kKnownAllocations fails.
            if (! result.skipped)
            {
                const auto v = mu_golden::judgeAllocations(def.name, allocProbe.take(), kKnownAllocations);
                for (const auto& n : v.notes) logMessage(n);
                for (const auto& f : v.failures) expect(false, f);
            }
        }
    }
};

static MuToniGoldenRenderTests muToniGoldenRenderTests;

//==============================================================================
// Pins: what the code does today, for the stages that will move it.
struct MuToniLayerPinTests : juce::UnitTest
{
    MuToniLayerPinTests() : juce::UnitTest("mu-Toni behaviour pins", "mu-toni-pins") {}

    void runTest() override
    {
        mu_seam_test::TempStorage storage;
        mu_seam_test::ScopedEnvVar deterministic("MU_DETERMINISTIC_NOISE", "1");
        if (mu_golden::GoldenStore::fromEnvironment().updateMode) return;   // recording a golden: the pins are not part of it
        readVoicePassthrough();
        readVoiceModulated();
        readVoiceInsertConfig();
        heldNoteStackAndRootByMidi();
        midiTriggerMode();
        structuralEditMidPlay();
        oversizeBlock();
    }

    // ── readVoice ────────────────────────────────────────────────────────────

    // With no assignment every modulation target publishes its raw parameter value, and each layer
    // reads its own a{N}_ parameters.
    void readVoicePassthrough()
    {
        beginTest("readVoice: unmodulated targets publish the raw value, per layer");
        Rig rig;
        rig.p.addLayer();
        for (int l = 0; l < 2; ++l)
        {
            const float k = (float) l;
            rig.set(layerId(l, "cut"), 4200.0f - 1500.0f * k);   rig.set(layerId(l, "res"), 0.55f - 0.2f * k);
            rig.set(layerId(l, "drv"), 0.2f + 0.1f * k);         rig.set(layerId(l, "noise"), -24.0f + 6.0f * k);
            rig.set(layerId(l, "o1l"), -2.0f - k);               rig.set(layerId(l, "o2l"), -9.0f + k);
            rig.set(layerId(l, "o2s"), 5.0f - 3.0f * k);         rig.set(layerId(l, "o1_pos"), 40.0f + 30.0f * k);
            rig.set(layerId(l, "o2_pos"), 200.0f - 50.0f * k);   rig.set(layerId(l, "xmod_index"), 35.0f + k);
            rig.set(layerId(l, "xmod_depth"), -20.0f + 5.0f * k); rig.set(layerId(l, "xmod_ssb"), 120.0f - 40.0f * k);
            rig.set(layerId(l, "feDep"), 0.6f - 0.3f * k);       rig.set(layerId(l, "aeL"), -3.0f - k);
            rig.set(layerId(l, "peDep"), 7.0f - 4.0f * k);       rig.set(layerId(l, "acc"), 45.0f + 10.0f * k);
            rig.set(layerId(l, "dir"), -30.0f + 20.0f * k);      rig.set(layerId(l, "octs"), 3.0f - k);
            rig.set(layerId(l, "inv"), -2.0f + 3.0f * k);        rig.set(layerId(l, "chord"), 7.0f + k);
            rig.set(layerId(l, "root"), 5.0f + k);               rig.set(layerId(l, "roct"), 2.0f + k);
            rig.set(layerId(l, "rate"), 4.0f + k);               rig.set(layerId(l, "gate"), 33.0f + k);
            rig.set(layerId(l, "porta"), 90.0f - 10.0f * k);
        }
        rig.block();

        int mismatches = 0;
        for (int l = 0; l < 2; ++l)
            for (int d = 0; d < mu_toni::kNumModDests; ++d)
            {
                const auto id = layerId(l, mu_toni::kModDestTable[d].param);
                const float raw = rig.p.apvts.getRawParameterValue(id)->load();
                const float live = rig.p.getModLive(l, d);
                if (std::abs(live - raw) > 1.0e-3f * std::max(1.0f, std::abs(raw)))
                {
                    ++mismatches;
                    logMessage("readVoice passthrough: layer " + juce::String(l) + " " + id + " raw " + juce::String(raw) + " live " + juce::String(live));
                }
            }
        expectEquals(mismatches, 0, "every target publishes its own layer's raw value");
    }

    // A stepped control sequence on cutoff and on the integer chord target (frozen state of the
    // 'modulation' scenario): the post-matrix values follow the sequence step by step, and the integer
    // target stays a plain float in modLive (rounding happens later, in the arp parameters).
    void readVoiceModulated()
    {
        beginTest("readVoice: a modulation assignment moves the published value on the sequence's steps");
        Rig rig;
        loadStateFile(rig.p, repoRoot().getChildFile("tests/golden/mu-toni/modulation/state.xml"));

        // cs0 steps 0, 0.8, 0.3, 1.0 every sixteenth (6000 frames at 120 bpm), read at the start of each step.
        const int blocksPerStep = (int) std::ceil(6000.0 / 512.0);
        for (int b = 0, step = 0; step < 4; ++b)
        {
            rig.block();
            if (b == step * blocksPerStep)
            {
                cutLive[step]   = rig.p.getModLive(0, mu_toni::D_cut);
                chordLive[step] = rig.p.getModLive(0, mu_toni::D_chord);
                ++step;
            }
        }
        // Depth is a percentage of the target's range in proportion space, so the skewed cutoff moves far
        // more at the top of its range than at the bottom. Recorded values (steps 0, 80, 30, 100 percent):
        const float cutExpected[4]   = { 1500.0f, 9499.33f, 3321.88f, 13520.1f };
        const float chordExpected[4] = { 3.0f, 8.44f, 5.04f, 9.8f };
        for (int i = 0; i < 4; ++i)
        {
            expectWithinAbsoluteError(cutLive[i], cutExpected[i], 0.5f);
            expectWithinAbsoluteError(chordLive[i], chordExpected[i], 0.01f);   // the integer target is published unrounded
        }
    }

    // The insert configuration of each layer is copied from its own parameters every block.
    void readVoiceInsertConfig()
    {
        beginTest("readVoice: the insert configuration comes from the layer's own parameters");
        Rig rig;
        rig.p.addLayer();
        rig.set("a0_drvChar", 7.0f);  rig.set("a0_insP1", 0.25f);  rig.set("a0_insP4", 0.75f);
        rig.set("a1_drvChar", 10.0f); rig.set("a1_insP2", 0.5f);   rig.set("a1_insP3", 0.125f);
        rig.block();
        expectEquals(rig.p.getArp(0).insertCfg.insertAlgo, 7);
        expectEquals(rig.p.getArp(1).insertCfg.insertAlgo, 10);
        expectWithinAbsoluteError(rig.p.getArp(0).insertCfg.insertParam[0], 0.25f, 1.0e-6f);
        expectWithinAbsoluteError(rig.p.getArp(0).insertCfg.insertParam[3], 0.75f, 1.0e-6f);
        expectWithinAbsoluteError(rig.p.getArp(1).insertCfg.insertParam[1], 0.5f, 1.0e-6f);
        expectWithinAbsoluteError(rig.p.getArp(1).insertCfg.insertParam[2], 0.125f, 1.0e-6f);
    }

    // ── Held-note stack and root-by-MIDI ─────────────────────────────────────

    struct RootStep
    {
        std::vector<juce::MidiMessage> midi;   // delivered at the start of the block
        int rootNote;                          // the parameter-driven root the same block must sound like (-1 = the base root)
        int rootOctave;
        int inChannel = -1;                    // MIDI In channel parameter value to set before the block (-1 = leave)
    };

    // The newest held MIDI note replaces the root (pitch class and octave). The pin renders a MIDI-driven
    // processor and a MIDI-free one whose root PARAMETERS are set to what the note should mean, block for
    // block, and requires the two renders to be bit-identical.
    void heldNoteStackAndRootByMidi()
    {
        beginTest("Held-note stack and root-by-MIDI: newest note wins, release reverts, clamp, channel filter, capacity");

        const int baseRoot = 9, baseOct = 2;
        auto note = [](int ch, int n, bool onOff) { return onOff ? on(ch, n) : off(ch, n); };
        std::vector<RootStep> steps;
        steps.push_back({ { note(1, 62, true), note(1, 67, true) },  7, 4 });                            // newest of two held wins
        steps.push_back({ { note(1, 67, false) },                    2, 3 });                            // release it: reverts to the one beneath
        steps.push_back({ { note(1, 55, true) },                     7, 3 });                            // a lower new note is still newest
        steps.push_back({ { note(1, 62, true) },                     2, 3 });                            // re-pressing a held note moves it to the top
        steps.push_back({ { juce::MidiMessage::allNotesOff(1) },    -1, -1 });                           // all notes off: back to the base root
        steps.push_back({ { note(1, 5, true) },                      5, 0 });                            // octave clamps up to 0
        steps.push_back({ { note(1, 127, true) },                    7, 8 });                            // octave clamps down to 8
        steps.push_back({ { note(1, 127, false) },                   5, 0 });
        steps.push_back({ { juce::MidiMessage::allSoundOff(1) },    -1, -1 });                           // all sound off clears too
        steps.push_back({ { note(7, 60, true) },                     0, 4 });                            // Omni takes any channel
        steps.push_back({ { juce::MidiMessage::allNotesOff(7) },    -1, -1 });
        steps.push_back({ {},                                       -1, -1, 3 });                        // the channel filter goes to 3
        steps.push_back({ { note(1, 64, true) },                    -1, -1 });                           // channel 1 note ignored
        steps.push_back({ { note(3, 71, true) },                    11, 4 });                            // channel 3 note taken
        steps.push_back({ { juce::MidiMessage::noteOn(3, 71, (juce::uint8) 0) }, -1, -1 });              // velocity-0 note-on releases
        {
            RootStep cap;                                                                                 // 33 distinct notes: the 33rd does not fit
            for (int n = 40; n <= 72; ++n) cap.midi.push_back(note(3, n, true));
            cap.rootNote = 71 % 12;  cap.rootOctave = 71 / 12 - 1;
            steps.push_back(cap);
        }

        Rig midiRig, paramRig;
        for (auto* r : { &midiRig, &paramRig })
        {
            r->set("a0_root", (float) baseRoot);  r->set("a0_roct", (float) baseOct);
            r->set("a0_rate", 6.0f);  r->set("a0_gate", 70.0f);
        }
        int differing = 0, idx = 0;
        for (const auto& s : steps)
        {
            if (s.inChannel >= 0) { midiRig.set("midiInCh", (float) s.inChannel);  }
            paramRig.set("a0_root", (float) (s.rootNote >= 0 ? s.rootNote : baseRoot));
            paramRig.set("a0_roct", (float) (s.rootOctave >= 0 ? s.rootOctave : baseOct));

            juce::MidiBuffer mb;
            for (const auto& m : s.midi) mb.addEvent(m, 0);
            // Four blocks per step so several arp steps sound on the new root.
            bool same = true;
            for (int b = 0; b < 4; ++b)
            {
                const auto a = midiRig.block(512, b == 0 ? mb : juce::MidiBuffer());
                const auto c = paramRig.block(512);
                same = same && a.hash.h == c.hash.h;
            }
            if (! same) { ++differing; logMessage("root-by-MIDI step " + juce::String(idx) + " differs from the parameter-driven render"); }
            ++idx;
        }
        expectEquals(differing, 0, "every step sounds exactly like the equivalent root parameters");
    }

    // ── MIDI-trigger mode ────────────────────────────────────────────────────

    void midiTriggerMode()
    {
        beginTest("MIDI-trigger mode: silent until a key, block-granular start, pattern restarts per note-on, stops on release");
        Rig rig;
        rig.set("a0_trig", 1.0f);  rig.set("a0_rate", 6.0f);  rig.set("a0_gate", 50.0f);

        // No key held: nothing fires and nothing sounds, even with the transport playing.
        float idlePeak = 0.0f;
        for (int b = 0; b < 8; ++b) idlePeak = std::max(idlePeak, rig.block().peak);
        expectEquals(rig.runner(0).stepsFired(), 0);
        expectEquals(idlePeak, 0.0f);

        // A key at sample 100 of a block: the first step still fires at the block's first sample.
        juce::MidiBuffer m1;  m1.addEvent(on(1, 60), 100);
        const auto first = rig.block(512, m1);
        expectEquals(rig.runner(0).lastStepOffset(), 0, "block-granular start (today)");
        expect(rig.runner(0).lastStep() >= 0);
        expect(first.peak > 0.0f, "sounds in the block of the note-on");
        const int firedAfterFirst = rig.runner(0).stepsFired();
        logMessage("steps fired in the first triggered block: " + juce::String(firedAfterFirst));

        // Held on: the pattern keeps counting.
        for (int b = 0; b < 14; ++b) rig.block();   // more than one 1/16 step (6000 frames) of host time
        expect(rig.runner(0).lastStep() > 0, "pattern advances while held");

        // A second note-on restarts the pattern at step 0.
        juce::MidiBuffer m2;  m2.addEvent(on(1, 64), 255);
        rig.block(512, m2);
        expect(rig.runner(0).lastStep() >= 0);
        const int firedAtRestart = rig.runner(0).stepsFired();
        logMessage("steps fired up to the restart block: " + juce::String(firedAtRestart) + ", last step " + juce::String(rig.runner(0).lastStep()));

        // Release one of two keys: still running; release the last: no new steps, the voice releases.
        juce::MidiBuffer m3;  m3.addEvent(off(1, 64), 0);
        rig.block(512, m3);
        const int firedWithOneKey = rig.runner(0).stepsFired();
        for (int b = 0; b < 14; ++b) rig.block();
        expect(rig.runner(0).stepsFired() > firedWithOneKey, "one key still held keeps the arp running");
        juce::MidiBuffer m4;  m4.addEvent(off(1, 60), 0);
        rig.block(512, m4);
        const int firedAtRelease = rig.runner(0).stepsFired();
        for (int b = 0; b < 4; ++b) rig.block();
        expectEquals(rig.runner(0).stepsFired(), firedAtRelease, "no steps once every key is up");
        expect(rig.runner(0).isActive(), "the release tail is still sounding just after the last key");
        for (int b = 0; b < 40; ++b) rig.block();
        expect(! rig.runner(0).isActive(), "the tail dies out");

        // Transport stopped with a key held: nothing runs.
        rig.host.playing = false;
        juce::MidiBuffer m5;  m5.addEvent(on(1, 60), 0);
        const int firedBeforeStopped = rig.runner(0).stepsFired();
        float stoppedPeak = 0.0f;
        for (int b = 0; b < 4; ++b) stoppedPeak = std::max(stoppedPeak, rig.block(512, b == 0 ? m5 : juce::MidiBuffer()).peak);
        expectEquals(rig.runner(0).stepsFired(), firedBeforeStopped, "a held key with the transport stopped fires nothing");
        expectEquals(stoppedPeak, 0.0f);
    }

    // ── Structural edit mid-play ─────────────────────────────────────────────

    // A structural edit raises layerEditInFlight, and the audio thread then skips rendering its blocks.
    // The skipped blocks run no layer: no note is released, no step fires, the host position moves on, and
    // MIDI is still absorbed into the held-note stack. The edit itself resets the audio state of every slot
    // whose layer moved and leaves the others alone. Recorded as today's behaviour.
    void structuralEditMidPlay()
    {
        beginTest("Structural edit mid-play: skipped blocks keep the note state; moved slots reset, the others keep their note");

        auto makeRig = [](Rig& rig, bool tied)
        {
            rig.p.addLayer();
            for (int l = 0; l < 2; ++l)
            {
                rig.set(layerId(l, "rate"), 6.0f);
                rig.set(layerId(l, "gate"), tied ? 100.0f : 50.0f);
                rig.set(layerId(l, "leg"), tied ? 1.0f : 0.0f);
                rig.set(layerId(l, "aeS"), 0.9f);
                rig.set(layerId(l, "aeR"), 1.0f);   // a long release, so "active" outlives any gate
            }
            for (int b = 0; b < 10; ++b) rig.block();
        };

        // (1) The audio thread skips blocks while the edit is in flight.
        {
            Rig rig;  makeRig(rig, true);
            expect(rig.runner(0).isActive() && rig.runner(1).isActive(), "both layers hold a tied note before the edit");
            const int fired0 = rig.runner(0).stepsFired(), fired1 = rig.runner(1).stepsFired();

            rig.p.layerEditInFlight.store(true);
            float skippedPeak = 0.0f;
            for (int b = 0; b < 4; ++b) skippedPeak = std::max(skippedPeak, rig.block().peak);
            rig.p.layerEditInFlight.store(false);

            expectEquals(skippedPeak, 0.0f, "skipped blocks are silent");
            expectEquals(rig.runner(0).stepsFired(), fired0, "no step fires in a skipped block");
            expectEquals(rig.runner(1).stepsFired(), fired1);
            expect(rig.runner(0).isActive() && rig.runner(1).isActive(), "no note is released by the skipped blocks");

            // A short skip (under half a step of host time) resumes with the missed steps fired together.
            const auto resumed = rig.block();
            const int burst = rig.runner(0).stepsFired() - fired0;
            logMessage("steps fired in the first block after a 4-block skip: " + juce::String(burst));
            expect(resumed.peak > 0.0f, "rendering resumes at once");
            expectEquals(burst, shortSkipBurst, "a step due in the skipped blocks fires late, in the first block after");
            expectEquals(rig.runner(0).lastStepOffset(), 0, "...at the block's first sample");
        }

        // (2) A longer skip (more than half a step of host time) re-finds the grid instead.
        {
            Rig rig;  makeRig(rig, true);
            const int fired0 = rig.runner(0).stepsFired();
            rig.p.layerEditInFlight.store(true);
            for (int b = 0; b < 8; ++b) rig.block();
            rig.p.layerEditInFlight.store(false);
            rig.block();
            const int burst = rig.runner(0).stepsFired() - fired0;
            logMessage("steps fired in the first block after an 8-block skip: " + juce::String(burst));
            expectEquals(burst, longSkipBurst, "a step due in the skipped blocks is dropped");
        }

        // (3) MIDI that arrives in a skipped block still reaches the held-note stack: the root follows it.
        {
            Rig midiRig, paramRig;  makeRig(midiRig, false);  makeRig(paramRig, false);
            midiRig.p.layerEditInFlight.store(true);
            juce::MidiBuffer mb;  mb.addEvent(on(1, 67), 0);
            midiRig.block(512, mb);
            midiRig.p.layerEditInFlight.store(false);
            paramRig.p.layerEditInFlight.store(true);
            paramRig.block();
            paramRig.p.layerEditInFlight.store(false);
            paramRig.set("a0_root", 7.0f);  paramRig.set("a0_roct", 4.0f);
            paramRig.set("a1_root", 7.0f);  paramRig.set("a1_roct", 4.0f);
            bool same = true;
            for (int b = 0; b < 6; ++b) same = same && midiRig.block().hash.h == paramRig.block().hash.h;
            expect(same, "a note-on in a skipped block still becomes the root afterwards");
        }

        // (4) The edits: which slots lose their note.
        {
            Rig rig;  makeRig(rig, true);
            rig.p.removeLayer(1);                         // the top slot goes; slot 0 is untouched
            expectEquals(rig.p.getNumLayers(), 1);
            expect(rig.runner(0).isActive(), "removing the top layer leaves slot 0's tied note alone");
        }
        {
            Rig rig;  makeRig(rig, true);
            const int firedBefore = rig.runner(0).stepsFired();
            rig.p.removeLayer(0);                         // layer 1 shifts down into slot 0
            expectEquals(rig.p.getNumLayers(), 1);
            expect(! rig.runner(0).isActive(), "the layer that moved into slot 0 starts silent");
            expectEquals(rig.runner(0).stepsFired(), firedBefore, "the step counter is not part of the reset (today)");
            expect(rig.runner(0).lastStep() >= 0);
            expectEquals(rig.block().peak, 0.0f, "no tail or note carries over; the first step after the edit is still ahead");
            float later = 0.0f;
            for (int b = 0; b < 4; ++b) later = std::max(later, rig.block().peak);
            expect(later > 0.0f, "slot 0 sounds again from a fresh note on the next step");
        }
        {
            Rig rig;  makeRig(rig, true);
            rig.p.swapLayers(0, 1);
            expect(! rig.runner(0).isActive() && ! rig.runner(1).isActive(), "a swap resets both exchanged slots");
        }
        {
            Rig rig;  makeRig(rig, true);
            rig.p.addLayer();                             // a third layer joins; the first two keep sounding
            expect(rig.runner(0).isActive() && rig.runner(1).isActive(), "adding a layer leaves the others' notes alone");
            expect(! rig.runner(2).isActive(), "the new layer starts silent");
        }
    }

    // ── A block larger than the prepared maximum ─────────────────────────────

    // A host block above the size given to prepareToPlay. Before mu-Toni rendered through the shared driver,
    // such a block overran the voice and mixer buffers sized at prepare (garbage near 1e38, then a crash), so
    // this pin held only the supported edge. The driver now chunks it into calls of at most the prepared size
    // (C8): a 4096-frame block on a 1024 preparation renders exactly what four 1024-frame blocks render, and no
    // layer or mixer call is longer than the prepared size. (The line moved on purpose at the stage that
    // adopted the driver.)
    void oversizeBlock()
    {
        beginTest("A block above the prepared size is chunked: 4096 frames on a 1024 preparation equal four 1024-frame blocks");
        Rig big(1024), small(1024);
        for (auto* r : { &big, &small }) { r->set("a0_rate", 6.0f); r->set("a0_gate", 70.0f); }
        juce::AudioBuffer<float> a(2, 4096), b(2, 4096);
        int differing = 0;
        float peak = 0.0f;
        for (int blk = 0; blk < 12; ++blk)
        {
            peak = std::max(peak, big.block(4096).peak);
            for (int c = 0; c < 2; ++c) a.copyFrom(c, 0, big.work, c, 0, 4096);
            for (int q = 0; q < 4; ++q)
            {
                small.block(1024);
                for (int c = 0; c < 2; ++c) b.copyFrom(c, q * 1024, small.work, c, 0, 1024);
            }
            for (int c = 0; c < 2; ++c)
                if (std::memcmp(a.getReadPointer(c), b.getReadPointer(c), 4096 * sizeof (float)) != 0) ++differing;
        }
        expectEquals(differing, 0, "the chunked block is bit-identical to the same frames in prepared-size blocks");
        expect(peak > 0.0f && peak < 4.0f && std::isfinite(peak), "it sounds and stays bounded");
        expect(big.p.getLayerRenderProbe().largestCall <= 1024, "no call is longer than the prepared size");
    }

    static juce::String layerId(int layer, const char* suffix) { return mu_toni::layerParamId(layer, suffix); }

    float cutLive[4] {}, chordLive[4] {};
    static constexpr int shortSkipBurst = 1;
    static constexpr int longSkipBurst  = 0;
};

static MuToniLayerPinTests muToniLayerPinTests;

