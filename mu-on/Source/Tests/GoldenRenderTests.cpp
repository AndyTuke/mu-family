// Layer-extraction safety net for mu-On, part 1: golden renders. Each scenario drives the REAL headless
// PluginProcessor through mu-core's GoldenRender.h and compares a bit-exact fingerprint of the master
// and every track's stem with the one recorded from the unextracted tree (tests/golden/mu-on/<name>/).
// The frozen inputs (state.xml, scenario.json) live beside the golden; a golden changes only through
// tests/scripts/update-golden.ps1, never in a commit that moves code.
//
// Part 2, the pins that describe behaviour a later stage will move, is ExtractionPinTests.cpp.
//
// Authoring the inputs: state.xml is a real processor state saved by the code below, and scenario.json
// is written from the Scenario itself, so neither is hand-edited. Run the test exe with
// MU_GOLDEN_AUTHOR_STATE=1 to rewrite them; the input hash then changes, which is a deliberate update.

// This translation unit carries the allocation probe's global operator new / delete (test executable only).
#define MU_ALLOCATION_PROBE_DEFINE_OPERATORS
#include "GoldenTestRig.h"
#include "Tests/GoldenAllocationCheck.h"
#include "Tests/GoldenRender.h"

#include <thread>

namespace
{
    // Blocks that allocate or free inside processBlock today, recorded with their exact counts (see
    // GoldenAllocationCheck.h). Informational: reported on every run, never an approval to allocate.
    const std::vector<mu_golden::KnownAllocation> kKnownAllocations = {
    };
    using mu_on::EngineType;
    using mu_on::PluginProcessor;
    using mu_golden_rig::OwnedProcessor;
    using mu_golden_rig::setParam;
    using mu_golden_rig::setRow;
    using mu_golden_rig::useTrack;

    // The repo root, from this source file (<root>/mu-on/Source/Tests/GoldenRenderTests.cpp).
    juce::File repoRoot()
    {
        return juce::File(__FILE__).getParentDirectory().getParentDirectory()
                                   .getParentDirectory().getParentDirectory();
    }

    juce::File scenarioDir(const juce::String& name)
    {
        return repoRoot().getChildFile("tests").getChildFile("golden").getChildFile("mu-on").getChildFile(name);
    }

    // One scenario: how its frozen state is built (authoring only) and what the render does.
    struct Def
    {
        juce::String name;
        int numStems = 0;
        juce::int64 lengthFrames = 96000;
        double tempoBpm = 120.0;
        double startBeat = 0.0;
        int preparedBlockSize = 1024;   // above the largest scheduled block, so no block is oversize
        std::vector<mu_golden::Schedule> schedules { mu_golden::Schedule::fixed512(), mu_golden::Schedule::ragged() };
        std::function<void(PluginProcessor&)> author;
        std::vector<mu_golden::ScriptedEvent> events;
        juce::String notes;
    };

    // Event helpers: parameter sets and host edits at a block index.
    mu_golden::ScriptedEvent setParamAt(int block, const juce::String& id, float value)
    {
        return { block, "set " + id + " = " + juce::String(value),
                 [id, value](mu_golden::ScriptContext& c) { setParam(c.proc.apvts, id, value); } };
    }

    // The groove every track in [0, tracks) shares in a scenario, set at block 0: each track has its own swing and
    // accent rows. The frozen state.xml of these scenarios predates the rows and carries the old global ones (which a
    // load ignores, as this also pins), so the values come in as events rather than through a re-authored state.
    std::vector<mu_golden::ScriptedEvent> grooveAtStart(int tracks, float swing, float accent, bool setSwing = true)
    {
        std::vector<mu_golden::ScriptedEvent> events;
        for (int t = 0; t < tracks; ++t)
        {
            if (setSwing) events.push_back(setParamAt(0, mu_on::layerParamId(t, "swing"), swing));
            events.push_back(setParamAt(0, mu_on::layerParamId(t, "accent"), accent));
        }
        return events;
    }

    // ── The scenarios ────────────────────────────────────────────────────────
    std::vector<Def> makeDefs()
    {
        std::vector<Def> defs;

        // A fresh instance: one Kick track, four on the floor, nothing changed.
        {
            Def d; d.name = "kick-default"; d.numStems = 1;
            d.notes = "Fresh instance, default Kick track and groove.";
            d.author = [](PluginProcessor&) {};
            defs.push_back(d);
        }

        // Bass: saw with sub, resonant filter envelope and drive, accented off-beat pattern.
        {
            Def d; d.name = "bass-saw"; d.numStems = 1;
            d.notes = "One Bass track: saw, sub, resonance, filter envelope, drive, partial sustain.";
            d.author = [](PluginProcessor& p)
            {
                useTrack(p, 0, EngineType::Bass);
                const auto id = [](const char* x) { return mu_on::engineParamId(0, EngineType::Bass, x); };
                setParam(p.apvts, id("wave"), 1.0f);   setParam(p.apvts, id("sub"), 0.6f);
                setParam(p.apvts, id("cut"), 400.0f);  setParam(p.apvts, id("res"), 0.55f);
                setParam(p.apvts, id("env"), 0.7f);    setParam(p.apvts, id("edec"), 220.0f);
                setParam(p.apvts, id("atk"), 3.0f);    setParam(p.apvts, id("dec"), 350.0f);
                setParam(p.apvts, id("sus"), 0.15f);   setParam(p.apvts, id("drive"), 0.45f);
                setRow(p, 0, "0010001000100110", "0010000000000100");
            };
            defs.push_back(d);
        }

        // Hat and Snare sample engines on two tracks, with accents and a lowered accent level.
        {
            Def d; d.name = "hat-snare"; d.numStems = 2;
            d.notes = "Hat track and Snare track; the sample engines with their own tune and decay.";
            d.author = [](PluginProcessor& p)
            {
                useTrack(p, 0, EngineType::Hat);
                useTrack(p, 1, EngineType::Snare);
                setRow(p, 0, "1010101010101010", "1000100010001000");
                setRow(p, 1, "0000100000001001", "0000100000000000");
                setParam(p.apvts, mu_on::engineParamId(0, EngineType::Hat, "dec"), 90.0f);
                setParam(p.apvts, mu_on::engineParamId(1, EngineType::Snare, "tune"), -2.0f);
                setParam(p.apvts, mu_on::engineParamId(1, EngineType::Snare, "dec"), 220.0f);
            };
            d.events = grooveAtStart(2, 0.0f, 0.9f, false);   // both tracks' accent 0.9 (the global one it replaces)
            defs.push_back(d);
        }

        // Rumble processing a Kick (source = track 0).
        {
            Def d; d.name = "rumble-kick"; d.numStems = 2; d.lengthFrames = 120000;
            d.notes = "Kick plus a Rumble track whose source is the Kick; tail past the last hit.";
            d.author = [](PluginProcessor& p)
            {
                useTrack(p, 1, EngineType::Rumble);
                setParam(p.apvts, mu_on::layerParamId(1, "src"), 0.0f);
                const auto id = [](const char* x) { return mu_on::engineParamId(1, EngineType::Rumble, x); };
                setParam(p.apvts, id("drive"), 0.5f);  setParam(p.apvts, id("size"), 0.6f);
                setParam(p.apvts, id("revmix"), 0.45f); setParam(p.apvts, id("cut"), 900.0f);
            };
            defs.push_back(d);
        }

        // Rumble whose source is missing, then valid, then missing again: silence, signal, ringing tail.
        {
            Def d; d.name = "rumble-source-switch"; d.numStems = 2; d.lengthFrames = 144000;
            d.notes = "Rumble source: a layer that is not live, then the Kick (block 40), then not live again (block 110).";
            d.author = [](PluginProcessor& p)
            {
                useTrack(p, 1, EngineType::Rumble);
                setParam(p.apvts, mu_on::layerParamId(1, "src"), 5.0f);
            };
            d.events = { setParamAt(40, mu_on::layerParamId(1, "src"), 0.0f),
                         setParamAt(110, mu_on::layerParamId(1, "src"), 5.0f) };
            defs.push_back(d);
        }

        // All five engine types at once with global swing and accent.
        {
            Def d; d.name = "groove-five-swing-accent"; d.numStems = 5;
            d.notes = "Kick, Bass, Hat, Snare, Rumble(source Kick); each track's t{N}_swing 0.35 and t{N}_accent 0.55, set at block 0.";
            d.author = [](PluginProcessor& p)
            {
                useTrack(p, 1, EngineType::Bass);
                useTrack(p, 2, EngineType::Hat);
                useTrack(p, 3, EngineType::Snare);
                useTrack(p, 4, EngineType::Rumble);
                setParam(p.apvts, mu_on::layerParamId(4, "src"), 0.0f);
                setRow(p, 0, "1000100010001001", "1000000010000000");
                setRow(p, 1, "0010001000100010", "0010000000100000");
                setRow(p, 2, "0101010101010101", "0001000100010001");
                setRow(p, 3, "0000100000001000", "0000100000000000");
            };
            d.events = grooveAtStart(5, 0.35f, 0.55f);
            defs.push_back(d);
        }

        // Bass ducked by the Kick through the shared mixer sidechain.
        {
            Def d; d.name = "sidechain-bass-kick"; d.numStems = 2;
            d.notes = "Kick and a sustaining Bass; the Bass channel ducks from the Kick channel (ch1_scSrc = 1).";
            d.author = [](PluginProcessor& p)
            {
                useTrack(p, 1, EngineType::Bass);
                setRow(p, 1, "1010101010101010", "0000000000000000");
                setParam(p.apvts, mu_on::engineParamId(1, EngineType::Bass, "sus"), 0.8f);
                setParam(p.apvts, mu_on::engineParamId(1, EngineType::Bass, "dec"), 600.0f);
                setParam(p.apvts, "ch1_scSrc", 1.0f);
                setParam(p.apvts, "ch1_scAmt", 0.85f);
                setParam(p.apvts, "ch1_scAtk", 3.0f);
                setParam(p.apvts, "ch1_scRel", 180.0f);
            };
            defs.push_back(d);
        }

        // The host stops, restarts where it stopped, then locates.
        {
            Def d; d.name = "transport-stop-start-locate"; d.numStems = 2;
            d.notes = "Stop at block 40, start at block 55 (same position), locate to beat 9.5 at block 85.";
            d.author = [](PluginProcessor& p)
            {
                useTrack(p, 1, EngineType::Bass);
                setRow(p, 1, "1010101010101010", "0000000000000000");
                setParam(p.apvts, mu_on::engineParamId(1, EngineType::Bass, "sus"), 0.7f);
            };
            d.events = {
                { 40, "host stops",  [](mu_golden::ScriptContext& c) { c.host.playing = false; } },
                { 55, "host starts", [](mu_golden::ScriptContext& c) { c.host.playing = true; } },
                { 85, "locate to beat 9.5", [](mu_golden::ScriptContext& c) { c.host.ppq = 9.5; } } };
            defs.push_back(d);
        }

        // Tempo moves while playing; the Rumble's delay taps are tempo-synced.
        {
            Def d; d.name = "tempo-change-midplay"; d.numStems = 3; d.lengthFrames = 120000;
            d.notes = "Kick, Hat and Rumble; tempo 120 -> 90 (block 30) -> 160 (block 80).";
            d.author = [](PluginProcessor& p)
            {
                useTrack(p, 1, EngineType::Hat);
                useTrack(p, 2, EngineType::Rumble);
                setParam(p.apvts, mu_on::layerParamId(2, "src"), 0.0f);
            };
            d.events = {
                { 30, "tempo 90",  [](mu_golden::ScriptContext& c) { c.host.bpm = 90.0; } },
                { 80, "tempo 160", [](mu_golden::ScriptContext& c) { c.host.bpm = 160.0; } } };
            defs.push_back(d);
        }

        // Modulators: a stepped sequence on the Kick's tune and decay, another on the Bass's cutoff.
        {
            Def d; d.name = "modulation-assignment"; d.numStems = 2;
            d.notes = "cs0 on the Kick (tune, decay) and cs1 on the Bass (cutoff), both depth-as-percent-of-range targets.";
            d.author = [](PluginProcessor& p)
            {
                useTrack(p, 1, EngineType::Bass);
                setRow(p, 1, "0010001000100010", "0000000000000000");
                auto stepped = [](ControlSequence& cs, std::vector<float> v)
                {
                    cs.mode = ControlSequence::Mode::Stepped;
                    cs.polarity = ControlSequence::Polarity::Bipolar;
                    cs.stepValues = std::move(v);
                };
                stepped(p.getTrack(0).controlSequences[0],
                        { 100, -60, 30, -100, 0, 80, -20, 60, 100, -60, 30, -100, 0, 80, -20, 60 });
                stepped(p.getTrack(1).controlSequences[1],
                        { 0, 100, 0, -100, 50, -50, 100, 0, 0, 100, 0, -100, 50, -50, 100, 0 });
                auto assign = [](Layer& layer, const char* id, const char* src, const char* dest, float depth)
                {
                    ModulationAssignment a;
                    a.id = id; a.sourceId = src; a.destinationId = dest; a.depth = depth;
                    [[maybe_unused]] const bool added = layer.modulationMatrix.addAssignment(a);
                    jassert(added);
                };
                assign(p.getTrack(0), "m1", "cs0_output", "k.tune.prop", 35.0f);
                assign(p.getTrack(0), "m2", "cs0_output", "k.adec.prop", 25.0f);
                assign(p.getTrack(1), "m3", "cs1_output", "b.cut.prop", 40.0f);
            };
            defs.push_back(d);
        }

        // Master inserts and the FX returns (effect, delay, reverb sends).
        {
            Def d; d.name = "master-insert-fx-return"; d.numStems = 3; d.lengthFrames = 120000;
            d.notes = "Kick, Hat, Snare through two master inserts and all three FX sends/returns.";
            d.author = [](PluginProcessor& p)
            {
                useTrack(p, 1, EngineType::Hat);
                useTrack(p, 2, EngineType::Snare);
                setRow(p, 1, "1010101010101010", "1000100010001000");
                setRow(p, 2, "0000100000001000", "0000000000000000");
                setParam(p.apvts, "ch0_sendEff", 0.2f);
                setParam(p.apvts, "ch1_sendDly", 0.4f);
                setParam(p.apvts, "ch2_sendRev", 0.5f);
                setParam(p.apvts, "ch2_sendDly", 0.25f);
                setParam(p.apvts, "ret_rev_lvl", 0.8f);
                setParam(p.apvts, "ret_dly_lvl", 0.6f);
                setParam(p.apvts, "mst_insChar", 10.0f);   // TapeSat
                setParam(p.apvts, "mst_insP1", 0.55f); setParam(p.apvts, "mst_insP2", 0.4f);
                setParam(p.apvts, "mst_ins2Char", 7.0f);   // Compressor
                setParam(p.apvts, "mst_ins2P1", 0.5f); setParam(p.apvts, "mst_ins2P2", 0.5f);
            };
            defs.push_back(d);
        }

        // Structural edits while playing (add, reorder, remove), master only because the layer count moves.
        {
            Def d; d.name = "structural-edit-midplay"; d.numStems = 0; d.lengthFrames = 144000;
            d.notes = "Kick and Hat; a Bass track is added (block 40), tracks 0 and 2 swap (block 70), track 1 is removed (block 110).";
            d.author = [](PluginProcessor& p)
            {
                useTrack(p, 1, EngineType::Hat);
                setParam(p.apvts, mu_on::engineParamId(0, EngineType::Kick, "adec"), 300.0f);
            };
            d.events = {
                { 40,  "addLayer",           [](mu_golden::ScriptContext& c) { c.proc.addLayer(); } },
                { 70,  "swapLayers(0, 2)",   [](mu_golden::ScriptContext& c) { c.proc.swapLayers(0, 2); } },
                { 110, "removeLayer(1)",     [](mu_golden::ScriptContext& c) { c.proc.removeLayer(1); } } };
            defs.push_back(d);
        }

        // Host automation of the engine choice mid-play, from another thread than the message thread (as a host's
        // audio thread delivers it): each change switches the track's engine in the block that sees it.
        {
            Def d; d.name = "engine-automation-midplay"; d.numStems = 3; d.lengthFrames = 120000;
            d.notes = "Kick, Snare, Rumble(source Kick); t0_eng, t1_eng and t2_eng automated from a non-message thread at blocks 30, 55, 80 and 100.";
            d.author = [](PluginProcessor& p)
            {
                useTrack(p, 1, EngineType::Snare);
                useTrack(p, 2, EngineType::Rumble);
                setParam(p.apvts, mu_on::layerParamId(2, "src"), 0.0f);
                setRow(p, 1, "1010101010101010", "1000100010001000");
                setRow(p, 2, "1000100010001000", "0000000000000000");
            };
            auto automate = [](int block, int track, EngineType e)
            {
                return mu_golden::ScriptedEvent { block, "automate t" + juce::String(track) + "_eng = " + juce::String(mu_on::engineName(e)),
                    [track, e](mu_golden::ScriptContext& c)
                    {
                        std::thread host([&c, track, e] { setParam(c.proc.apvts, mu_on::layerParamId(track, "eng"), (float) mu_on::toIndex(e)); });
                        host.join();
                    } };
            };
            d.events = { automate(30, 0, EngineType::Bass), automate(55, 1, EngineType::Hat),
                         automate(80, 0, EngineType::Kick), automate(100, 2, EngineType::Snare) };
            defs.push_back(d);
        }

        // Contract pin P7: host blocks above the prepared size (512 here, so ragged's 1024 is one too), which
        // the shared driver chunks into calls of at most the prepared size (C8). Before the driver an oversize
        // block ran off the end of the mixer's channel buffers, so this segment could not be recorded. The
        // state is groove-five-swing-accent's (all five engines, the Rumble on the Kick, swing and accent).
        {
            Def d; d.name = "oversize-block"; d.numStems = 5; d.preparedBlockSize = 512;
            d.notes = "P7: the five-engine groove on a 512 preparation; ragged's 1024 and the oversize schedule's 4096 and 2048 are chunked (C8).";
            d.schedules.push_back({ "oversize", { 4096, 1, 2048 } });
            d.author = [](PluginProcessor& p)
            {
                useTrack(p, 1, EngineType::Bass);
                useTrack(p, 2, EngineType::Hat);
                useTrack(p, 3, EngineType::Snare);
                useTrack(p, 4, EngineType::Rumble);
                setParam(p.apvts, mu_on::layerParamId(4, "src"), 0.0f);
                setRow(p, 0, "1000100010001001", "1000000010000000");
                setRow(p, 1, "0010001000100010", "0010000000100000");
                setRow(p, 2, "0101010101010101", "0001000100010001");
                setRow(p, 3, "0000100000001000", "0000100000000000");
            };
            d.events = grooveAtStart(5, 0.35f, 0.55f);
            defs.push_back(d);
        }

        // FM Perc: the default Tom (90 Hz, the APVTS defaults) on four steps with two accents.
        {
            Def d; d.name = "fmperc-tom-default"; d.numStems = 1;
            d.notes = "One FM Perc track, algorithm Tom at its APVTS defaults (Tune 90 Hz); four hits, two accented.";
            d.author = [](PluginProcessor& p)
            {
                useTrack(p, 0, EngineType::FmPerc);
                setRow(p, 0, "1000001010000010", "1000000000000010");
            };
            defs.push_back(d);
        }

        return defs;
    }

    // scenario.json, written from the Scenario so it cannot drift from what runs.
    juce::String describe(const Def& d)
    {
        juce::String s;
        s << "{\n";
        s << "  \"name\": \"" << d.name << "\",\n";
        s << "  \"notes\": \"" << d.notes << "\",\n";
        s << "  \"sampleRate\": 48000,\n";
        s << "  \"preparedBlockSize\": " << d.preparedBlockSize << ",\n";
        s << "  \"lengthFrames\": " << d.lengthFrames << ",\n";
        s << "  \"tempoBpm\": " << d.tempoBpm << ",\n";
        s << "  \"startBeat\": " << d.startBeat << ",\n";
        s << "  \"numStems\": " << d.numStems << ",\n";
        s << "  \"schedules\": [";
        for (size_t i = 0; i < d.schedules.size(); ++i)
        {
            s << (i ? ", " : "") << "{ \"name\": \"" << d.schedules[i].name << "\", \"blocks\": [";
            for (size_t k = 0; k < d.schedules[i].blocks.size(); ++k)
                s << (k ? ", " : "") << d.schedules[i].blocks[k];
            s << "] }";
        }
        s << "],\n";
        s << "  \"events\": [";
        for (size_t i = 0; i < d.events.size(); ++i)
            s << (i ? ", " : "") << "{ \"block\": " << d.events[i].block << ", \"label\": \"" << d.events[i].label << "\" }";
        s << "]\n}\n";
        return s;
    }

    // Writes state.xml and scenario.json for one scenario from a freshly built processor.
    void writeInputs(const Def& d)
    {
        OwnedProcessor owned;
        d.author(owned);
        juce::MemoryBlock state;
        owned.getStateInformation(state);
        const auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), (int) state.getSize());
        const auto dir = scenarioDir(d.name);
        dir.createDirectory();
        dir.getChildFile("state.xml").replaceWithText(xml->toString());
        dir.getChildFile("scenario.json").replaceWithText(describe(d));
    }

    mu_golden::Scenario makeScenario(const Def& d)
    {
        const auto dir = scenarioDir(d.name);
        mu_golden::Scenario sc;
        sc.name = d.name;
        sc.sampleRate = 48000.0;
        sc.preparedBlockSize = d.preparedBlockSize;
        sc.lengthFrames = d.lengthFrames;
        sc.tempoBpm = d.tempoBpm;
        sc.startBeat = d.startBeat;
        sc.numStems = d.numStems;
        sc.schedules = d.schedules;
        sc.events = d.events;
        const auto stateFile = dir.getChildFile("state.xml");
        sc.inputHash = mu_golden::hashFiles({ stateFile, dir.getChildFile("scenario.json") });
        sc.loadState = [stateFile](ProcessorBase& pb)
        {
            if (const auto xml = juce::parseXML(stateFile))
            {
                juce::MemoryBlock mb;
                juce::AudioProcessor::copyXmlToBinary(*xml, mb);
                pb.setStateInformation(mb.getData(), (int) mb.getSize());
            }
        };
        return sc;
    }

    bool authoringRequested()
    {
        return juce::SystemStats::getEnvironmentVariable("MU_GOLDEN_AUTHOR_STATE", {}) == "1";
    }
}

struct MuOnGoldenRenderTests : juce::UnitTest
{
    MuOnGoldenRenderTests() : juce::UnitTest("mu-On golden renders", "mu-on-golden") {}

    // Fails on heap activity that is not in kKnownAllocations; logs the listed ones.
    void reportAllocations(const juce::String& scenario, const std::vector<mu_golden::AllocFinding>& found)
    {
        const auto v = mu_golden::judgeAllocations(scenario, found, kKnownAllocations);
        for (const auto& n : v.notes) logMessage(n);
        for (const auto& f : v.failures) expect(false, f);
    }

    void runTest() override
    {
        const auto defs = makeDefs();

        if (authoringRequested())
        {
            beginTest("authoring: write state.xml and scenario.json for every scenario");
            for (const auto& d : defs) { writeInputs(d); logMessage("wrote inputs for " + d.name); }
            return;
        }

        beginTest("allocation probe sees heap use and ignores debug-STL proxies");
        for (const auto& f : mu_golden::probeSelfTest()) expect(false, f);

        auto store = mu_golden::GoldenStore::fromEnvironment();
        mu_golden::AllocationProbeCounter allocProbe;
        store.allocationCounter = &allocProbe;
        const mu_golden::ProcessorFactory factory = [] { return std::unique_ptr<ProcessorBase>(new OwnedProcessor()); };

        for (const auto& d : defs)
        {
            const auto sc = makeScenario(d);
            const bool skipThis = store.updateMode && store.scenarioFilter != "mu-on/" + d.name;

            beginTest(d.name + ": inputs exist");
            expect(scenarioDir(d.name).getChildFile("state.xml").existsAsFile(), "state.xml missing; run with MU_GOLDEN_AUTHOR_STATE=1");
            if (! scenarioDir(d.name).getChildFile("state.xml").existsAsFile()) continue;

            if (! skipThis)
            {
                beginTest(d.name + ": renders the same twice in one process");
                const auto rep = store.verifyRepeatable(sc, factory);
                expect(rep.ok, rep.message);
            }

            beginTest(d.name + ": matches the golden");
            allocProbe.setScenario(d.name);
            const auto out = store.verify("mu-on", sc, factory);
            if (out.skipped) continue;
            expect(out.ok, out.message);
            if (out.message.isNotEmpty()) logMessage(out.message);

            beginTest(d.name + ": no heap allocation or free inside processBlock");
            reportAllocations(d.name, allocProbe.take());
        }
    }
};

static MuOnGoldenRenderTests muOnGoldenRenderTests;

