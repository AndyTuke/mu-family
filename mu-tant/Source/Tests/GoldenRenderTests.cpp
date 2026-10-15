// Layer-extraction safety net for mu-Tant, part 1: golden renders. Each scenario drives the REAL headless
// PluginProcessor through mu-core's GoldenRender.h and compares a bit-exact fingerprint of the master and
// every voice's stem with the one recorded from the unextracted tree (tests/golden/mu-tant/<name>/). The
// frozen inputs (state.xml, scenario.json) live beside the golden; a golden changes only through
// tests/scripts/update-golden.ps1, never in a commit that moves code.
//
// Part 2, the pins that describe behaviour a later stage will move, is ExtractionPinTests.cpp.
//
// Authoring the inputs: state.xml is a real processor state saved by the code below, and scenario.json is
// written from the Scenario itself, so neither is hand-edited. Run the test exe with MU_GOLDEN_AUTHOR_STATE=1
// to rewrite them; the input hash then changes, which is a deliberate update.
//
// Time in a scenario: parameter changes, host edits and structural edits are TIMED ACTIONS, run by the
// first block that starts at or after their frame, so every block schedule meets them at the same moment
// (the harness's own events are block indices, which land at different times under a ragged schedule). Only
// MIDI is still placed by block index, as the harness offers nothing else; under the ragged schedule a note
// therefore sounds earlier than under fixed512, which is deterministic and still exercises the same code.

// This translation unit carries the allocation probe's global operator new / delete (test executable only).
#define MU_ALLOCATION_PROBE_DEFINE_OPERATORS
#include "GoldenTestRig.h"
#include "Tests/GoldenAllocationCheck.h"
#include "Tests/GoldenRender.h"

namespace
{
    // Blocks that allocate or free inside processBlock today, recorded with their exact counts (see
    // GoldenAllocationCheck.h). Informational: reported on every run, never an approval to allocate.
    const std::vector<mu_golden::KnownAllocation> kKnownAllocations = {
    };
    using mu_golden_rig::OwnedProcessor;
    using mu_golden_rig::openGate;
    using mu_golden_rig::setParam;
    using mu_golden_rig::setVoice;
    using mu_tant::GateEnvelope;
    using mu_tant::PluginProcessor;

    // The repo root, from this source file (<root>/mu-tant/Source/Tests/GoldenRenderTests.cpp).
    juce::File repoRoot()
    {
        return juce::File(__FILE__).getParentDirectory().getParentDirectory()
                                   .getParentDirectory().getParentDirectory();
    }

    juce::File scenarioDir(const juce::String& name)
    {
        return repoRoot().getChildFile("tests").getChildFile("golden").getChildFile("mu-tant").getChildFile(name);
    }

    // Something that happens once, at the first block starting at or after `frame`.
    struct Timed
    {
        juce::int64 frame;
        juce::String label;
        std::function<void(OwnedProcessor&, mu_golden::MockHost&)> run;
    };

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
        std::vector<Timed> timed;
        std::vector<mu_golden::ScriptedMidi> midi;
        bool drainDeferredWorkEveryBlock = false;   // the message thread's hot-swap poll, once per block
        bool isolateStemsByMute = false;            // a stem mutes the other strips instead of soloing (see makeScenario)
        juce::String notes;
    };

    // ── Authoring helpers ────────────────────────────────────────────────────
    void gate(PluginProcessor& p, int voice, int bars, std::initializer_list<GateEnvelope> envs)
    {
        auto& g = p.getPattern(voice).gate;
        g.patternLengthBars.store(bars);
        for (const auto& e : envs) g.addEnvelope(e);
    }

    void filterEnv(PluginProcessor& p, int voice, int bars, std::initializer_list<GateEnvelope> envs)
    {
        auto& g = p.getPattern(voice).filterGate;
        g.patternLengthBars.store(bars);
        for (const auto& e : envs) g.addEnvelope(e);
    }

    void pitchEnv(PluginProcessor& p, int voice, int bars, std::initializer_list<GateEnvelope> envs)
    {
        auto& g = p.getPattern(voice).pitchGate;
        g.patternLengthBars.store(bars);
        for (const auto& e : envs) g.addEnvelope(e);
    }

    Timed setAt(juce::int64 frame, int voice, const char* base, float value)
    {
        return { frame, juce::String(base) + " = " + juce::String(value),
                 [voice, base, value](OwnedProcessor& o, mu_golden::MockHost&) { setVoice(o.apvts, voice, base, value); } };
    }

    Timed setGlobalAt(juce::int64 frame, const char* id, float value)
    {
        return { frame, juce::String(id) + " = " + juce::String(value),
                 [id, value](OwnedProcessor& o, mu_golden::MockHost&) { setParam(o.apvts, id, value); } };
    }

    // A host or saved-state blob of the processor as it is now, with the voice count rewritten: what a
    // DAW hands setStateInformation when a project is reloaded or an undo step is restored.
    juce::MemoryBlock stateWithVoices(PluginProcessor& p, int voices)
    {
        auto tree = p.captureFullPreset();
        tree.setProperty("numVoices", voices, nullptr);
        juce::MemoryBlock mb;
        if (auto xml = tree.createXml()) juce::AudioProcessor::copyXmlToBinary(*xml, mb);
        return mb;
    }

    Timed restoreAt(juce::int64 frame, int voices)
    {
        return { frame, "host restores a state with " + juce::String(voices) + " voices",
                 [voices](OwnedProcessor& o, mu_golden::MockHost&)
                 {
                     const auto mb = stateWithVoices(o, voices);
                     o.setStateInformation(mb.getData(), (int) mb.getSize());
                 } };
    }

    // Stages a full preset with `voices` voices through the hot-swap path: it commits at the next loop
    // boundary of voice 0, and (while playing) retires any voice it drops.
    Timed stageAt(juce::int64 frame, int voices)
    {
        return { frame, "stage a full preset with " + juce::String(voices) + " voices (commits at the loop)",
                 [voices](OwnedProcessor& o, mu_golden::MockHost&)
                 {
                     auto tree = o.captureFullPreset();
                     tree.setProperty("numVoices", voices, nullptr);
                     o.useLoadedFullPreset(std::move(tree));
                 } };
    }

    mu_golden::ScriptedMidi noteOn(int block, int offset, int note)
    {
        return { block, offset, juce::MidiMessage::noteOn(1, note, (juce::uint8) 100) };
    }
    mu_golden::ScriptedMidi noteOff(int block, int offset, int note)
    {
        return { block, offset, juce::MidiMessage::noteOff(1, note) };
    }

    // ── The scenarios ────────────────────────────────────────────────────────
    std::vector<Def> makeDefs()
    {
        std::vector<Def> defs;

        // Two wavetable oscillators with the filter, deterministic pink noise and a mid-render morph.
        {
            Def d; d.name = "drone-dual-osc-noise"; d.numStems = 1; d.lengthFrames = 96000;
            d.notes = "One voice, gater open: Saw Morph and Pulse oscillators detuned a fifth apart, pink noise (MU_DETERMINISTIC_NOISE), resonant lowpass; osc1 position sweeps at 1 s.";
            d.author = [](PluginProcessor& p)
            {
                openGate(p.apvts, 0);
                setVoice(p.apvts, 0, "o1_wt", 1);   setVoice(p.apvts, 0, "o1_pos", 90);
                setVoice(p.apvts, 0, "o2_wt", 2);   setVoice(p.apvts, 0, "o2_pos", 60);
                setVoice(p.apvts, 0, "o2_semi", 7); setVoice(p.apvts, 0, "o2_fine", 12);
                setVoice(p.apvts, 0, "noise_lvl", -18); setVoice(p.apvts, 0, "noise_type", 1);
                setVoice(p.apvts, 0, "flt_cut", 2400); setVoice(p.apvts, 0, "flt_res", 0.35f);
            };
            d.timed = { setAt(48000, 0, "o1_pos", 200) };
            defs.push_back(d);
        }

        // Cross-modulation: every phase mode (FM, PM, TZFM) and every amplitude mode (AM, RM, SSB), sync and feedback.
        {
            Def d; d.name = "xmod-fm-pm-ring-ssb"; d.numStems = 1; d.lengthFrames = 144000;
            d.notes = "Lane A FM (0 s), PM (0.5 s), TZFM (1 s); lane B AM, RM (1.5 s), SSB with a shift (2 s); sync and feedback on at 2.5 s.";
            d.author = [](PluginProcessor& p)
            {
                openGate(p.apvts, 0);
                setVoice(p.apvts, 0, "o1_wt", 0); setVoice(p.apvts, 0, "o2_wt", 3);
                setVoice(p.apvts, 0, "o2_semi", 0); setVoice(p.apvts, 0, "o2_fine", 7);
                setVoice(p.apvts, 0, "xmod_phaseMode", 0); setVoice(p.apvts, 0, "xmod_index", 45);
                setVoice(p.apvts, 0, "xmod_ampMode", 0);   setVoice(p.apvts, 0, "xmod_depth", 60);
                setVoice(p.apvts, 0, "flt_cut", 6000);
            };
            d.timed = { setAt(24000, 0, "xmod_phaseMode", 1), setAt(24000, 0, "xmod_index", 70),
                        setAt(48000, 0, "xmod_phaseMode", 2),
                        setAt(72000, 0, "xmod_ampMode", 1),    setAt(72000, 0, "xmod_depth", -50),
                        setAt(96000, 0, "xmod_ampMode", 2),    setAt(96000, 0, "xmod_depth", 80), setAt(96000, 0, "xmod_ssb", 137),
                        setAt(120000, 0, "sync", 1),           setAt(120000, 0, "xmod_fdbk", 1) };
            defs.push_back(d);
        }

        // Dual filter: series, then parallel, then a highpass second filter.
        {
            Def d; d.name = "dual-filter-series-parallel"; d.numStems = 1; d.lengthFrames = 144000;
            d.notes = "Saw Morph drone through LP12 -> BP12 in series; parallel at 1 s; filter 2 becomes a 3 kHz HP12 at 2 s.";
            d.author = [](PluginProcessor& p)
            {
                openGate(p.apvts, 0);
                setVoice(p.apvts, 0, "o1_wt", 1); setVoice(p.apvts, 0, "o1_pos", 200);
                setVoice(p.apvts, 0, "o2_lvl", -60);
                setVoice(p.apvts, 0, "flt_type", 0); setVoice(p.apvts, 0, "flt_cut", 1200); setVoice(p.apvts, 0, "flt_res", 0.5f);
                setVoice(p.apvts, 0, "flt_drv", 0.2f); setVoice(p.apvts, 0, "flt_lo_cut", 120);
                setVoice(p.apvts, 0, "flt2_type", 2); setVoice(p.apvts, 0, "flt2_cut", 900); setVoice(p.apvts, 0, "flt2_res", 0.4f);
                setVoice(p.apvts, 0, "flt_series", 1);
            };
            d.timed = { setAt(48000, 0, "flt_series", 0),
                        setAt(96000, 0, "flt2_type", 1), setAt(96000, 0, "flt2_cut", 3000) };
            defs.push_back(d);
        }

        // The gate pattern across several steps, with gap, a bend, a reversed region and a loop wrap.
        {
            Def d; d.name = "gate-pattern-steps"; d.numStems = 1; d.lengthFrames = 192000;
            d.notes = "One bar of 16 cells, five regions (instant decay, mid split, bent, near-pure attack, reversed) with 25% gap; two loops at 120 bpm.";
            d.author = [](PluginProcessor& p)
            {
                gate(p, 0, 1, { { 0, 2, 0.0f, 0.0f, 0.0f, false }, { 4, 1, 0.3f, 0.0f, 0.0f, false },
                                { 6, 3, 0.5f, 0.4f, -0.4f, false }, { 10, 2, 0.9f, 0.0f, 0.0f, false },
                                { 13, 3, 0.2f, 0.0f, 0.0f, true } });
                setVoice(p.apvts, 0, "gate_gap", 25);
                setVoice(p.apvts, 0, "o1_wt", 4); setVoice(p.apvts, 0, "o1_pos", 120);
                setVoice(p.apvts, 0, "flt_cut", 3500);
            };
            defs.push_back(d);
        }

        // The three drawable envelopes together: gate, filter cutoff and pitch.
        {
            Def d; d.name = "envelopes-gate-filter-pitch"; d.numStems = 1; d.lengthFrames = 192000;
            d.notes = "Gate over two bars, a one-bar filter envelope (depth 0.8 on filter 1, 0.5 on filter 2) and a one-bar pitch envelope (osc1 +7, osc2 -5 semitones).";
            d.author = [](PluginProcessor& p)
            {
                gate(p, 0, 2, { { 0, 6, 0.1f, 0.0f, 0.0f, false }, { 8, 4, 0.4f, 0.0f, 0.0f, false }, { 16, 8, 0.0f, 0.0f, 0.0f, false } });
                filterEnv(p, 0, 1, { { 0, 8, 0.2f, 0.0f, 0.0f, false }, { 8, 6, 0.5f, 0.3f, 0.0f, false } });
                pitchEnv(p, 0, 1, { { 0, 4, 0.0f, 0.0f, 0.0f, false }, { 8, 8, 0.7f, 0.0f, 0.0f, false } });
                setVoice(p.apvts, 0, "flt_cut", 400);        setVoice(p.apvts, 0, "flt_res", 0.4f);
                setVoice(p.apvts, 0, "flt_env_depth", 0.8f); setVoice(p.apvts, 0, "flt2_env_depth", 0.5f);
                setVoice(p.apvts, 0, "flt2_cut", 600);       setVoice(p.apvts, 0, "flt2_res", 0.2f);
                setVoice(p.apvts, 0, "o1_penv_depth", 7);    setVoice(p.apvts, 0, "o2_penv_depth", -5);
                setVoice(p.apvts, 0, "o1_wt", 1);            setVoice(p.apvts, 0, "o1_pos", 150);
            };
            defs.push_back(d);
        }

        // Note mode: the drone sounds only while a MIDI note is held, last note wins, the master mix is gated.
        {
            Def d; d.name = "note-mode-held-notes"; d.numStems = 1; d.lengthFrames = 96000;
            d.notes = "Note mode on: 60 held, 67 on top (pitch follows the last note), 64 on top of that, 64 off (back to 67), 67 off, 60 off (gate closes), then 48 on and off. No sends, so stem and master are the same signal.";
            d.author = [](PluginProcessor& p) { openGate(p.apvts, 0); setVoice(p.apvts, 0, "o1_wt", 1); setVoice(p.apvts, 0, "flt_cut", 5000); };
            d.timed = { { 0, "Note mode on", [](OwnedProcessor& o, mu_golden::MockHost&) { o.setMidiNoteMode(1); } } };
            d.midi = { noteOn(4, 100, 60), noteOn(40, 0, 67), noteOn(70, 300, 64), noteOff(100, 0, 64),
                       noteOff(130, 0, 67), noteOff(150, 0, 60), noteOn(175, 0, 48), noteOff(180, 0, 48) };
            defs.push_back(d);
        }

        // Note mode with delay and reverb sends: the gate multiplies the FINAL mix, so the FX return is gated too.
        {
            Def d; d.name = "note-mode-fx-tail"; d.numStems = 1; d.lengthFrames = 144000;
            d.notes = "Note mode on with reverb and delay sends: the note is released at block 40, so the stem (sends 0) ends there while the final mix is gated shut with the FX return still ringing; a second short note at block 120.";
            d.author = [](PluginProcessor& p)
            {
                openGate(p.apvts, 0); setVoice(p.apvts, 0, "o1_wt", 1); setVoice(p.apvts, 0, "flt_cut", 4000);
                setParam(p.apvts, "ch0_sendRev", 0.8f); setParam(p.apvts, "ch0_sendDly", 0.6f);
                setParam(p.apvts, "ret_rev_lvl", 0.9f); setParam(p.apvts, "ret_dly_lvl", 0.8f);
                setParam(p.apvts, "rev_size", 0.9f);    setParam(p.apvts, "dly_fb", 0.6f);
            };
            d.timed = { { 0, "Note mode on", [](OwnedProcessor& o, mu_golden::MockHost&) { o.setMidiNoteMode(1); } } };
            d.midi = { noteOn(4, 0, 60), noteOff(40, 0, 60), noteOn(120, 0, 64), noteOff(125, 0, 64) };
            defs.push_back(d);
        }

        // Two voices with their own gates, pitch and pan.
        {
            Def d; d.name = "two-layers-structural-edit"; d.numStems = 2; d.lengthFrames = 144000;
            d.notes = "Two voices with gates, pan and octave apart; a third drone is added (0.75 s), voices 0 and 2 swap (1.5 s), voice 1 is removed (2.25 s). Stems follow the slot, not the voice.";
            d.author = [](PluginProcessor& p)
            {
                mu_golden_rig::growTo(p, 2);
                gate(p, 0, 1, { { 0, 3, 0.0f, 0.0f, 0.0f, false }, { 8, 4, 0.2f, 0.0f, 0.0f, false } });
                gate(p, 1, 1, { { 4, 4, 0.3f, 0.0f, 0.0f, false }, { 12, 3, 0.0f, 0.0f, 0.0f, false } });
                setVoice(p.apvts, 1, "o1_oct", -1); setVoice(p.apvts, 1, "o1_wt", 2);
                setParam(p.apvts, "ch0_pan", -0.4f); setParam(p.apvts, "ch1_pan", 0.5f);
            };
            d.timed = { { 36000, "addLayer, open its gate", [](OwnedProcessor& o, mu_golden::MockHost&)
                          { o.addLayer(); openGate(o.apvts, 2); setVoice(o.apvts, 2, "o1_oct", 1); } },
                        { 72000, "swapLayers(0, 2)",  [](OwnedProcessor& o, mu_golden::MockHost&) { o.swapLayers(0, 2); } },
                        { 108000, "removeLayer(1)",   [](OwnedProcessor& o, mu_golden::MockHost&) { o.removeLayer(1); } } };
            defs.push_back(d);
        }

        // Modulators: a stepped sequence on the pitch, a smooth one on the cutoff, a stepped one on the cross-mod index.
        {
            Def d; d.name = "modulation-assignments"; d.numStems = 1; d.lengthFrames = 144000;
            d.notes = "cs0 stepped on osc1 semitone (melody), cs1 smooth on cutoff, cs2 stepped unipolar on X-Mod index and level; all depth-as-percent-of-range targets.";
            d.author = [](PluginProcessor& p)
            {
                openGate(p.apvts, 0);
                setVoice(p.apvts, 0, "o1_wt", 1); setVoice(p.apvts, 0, "o2_wt", 3);
                setVoice(p.apvts, 0, "xmod_index", 30); setVoice(p.apvts, 0, "flt_cut", 1500);
                setVoice(p.apvts, 0, "level", -14);
                auto& pat = p.getPattern(0);
                auto& cs0 = pat.controlSequences[0];
                cs0.mode = ControlSequence::Mode::Stepped;
                cs0.polarity = ControlSequence::Polarity::Bipolar;
                cs0.stepValues = { 100, 0, 50, -50, 100, -100, 0, 50, 100, 0, 50, -50, 0, 25, -25, 0 };
                auto& cs1 = pat.controlSequences[1];
                cs1.mode = ControlSequence::Mode::Smooth;
                cs1.polarity = ControlSequence::Polarity::Bipolar;
                cs1.loopMultiplier = 2;
                cs1.curvePoints = { { 0.0f, -1.0f }, { 0.5f, 1.0f }, { 1.0f, -1.0f } };
                auto& cs2 = pat.controlSequences[2];
                cs2.mode = ControlSequence::Mode::Stepped;
                cs2.polarity = ControlSequence::Polarity::Unipolar;
                cs2.stepValues = { 0, 100, 20, 80, 40, 60, 0, 100 };
                cs2.stepNoteValue = NoteValue::Eighth;
                auto assign = [&](const char* id, const char* src, const char* dest, float depth)
                {
                    ModulationAssignment a;
                    a.id = id; a.sourceId = src; a.destinationId = dest; a.depth = depth;
                    [[maybe_unused]] const bool added = pat.modulationMatrix.addAssignment(a);
                    jassert(added);
                };
                assign("m1", "cs0_output", "osc1.semi", 40.0f);
                assign("m2", "cs1_output", "filter.cutoff", 35.0f);
                assign("m3", "cs2_output", "xmod.index", 30.0f);
                assign("m4", "cs2_output", "level", 12.0f);
                p.refreshPitchQuantFlags(0);
            };
            defs.push_back(d);
        }

        // Per-voice inserts, master inserts and the three FX returns.
        {
            Def d; d.name = "inserts-master-fx-return"; d.numStems = 2; d.lengthFrames = 144000;
            d.notes = "Voice 0 through TapeSat (Bitcrusher from 1.5 s), voice 1 through Karplus; two master inserts (compressor, tape); effect, delay and reverb sends and returns.";
            d.author = [](PluginProcessor& p)
            {
                mu_golden_rig::growTo(p, 2);
                openGate(p.apvts, 0); openGate(p.apvts, 1);
                setVoice(p.apvts, 0, "o1_wt", 1); setVoice(p.apvts, 1, "o1_wt", 5); setVoice(p.apvts, 1, "o1_oct", 1);
                setVoice(p.apvts, 0, "drvChar", 10); setVoice(p.apvts, 0, "insP1", 0.6f); setVoice(p.apvts, 0, "insP2", 0.4f);
                setVoice(p.apvts, 1, "drvChar", 11); setVoice(p.apvts, 1, "insP1", 0.5f); setVoice(p.apvts, 1, "insP2", 0.7f);
                setParam(p.apvts, "ch0_sendEff", 0.3f); setParam(p.apvts, "ch0_sendDly", 0.3f);
                setParam(p.apvts, "ch1_sendRev", 0.5f); setParam(p.apvts, "ch1_sendDly", 0.2f);
                setParam(p.apvts, "ret_rev_lvl", 0.8f); setParam(p.apvts, "ret_dly_lvl", 0.6f); setParam(p.apvts, "ret_eff_lvl", 0.7f);
                setParam(p.apvts, "mst_insChar", 7.0f);  setParam(p.apvts, "mst_insP1", 0.5f); setParam(p.apvts, "mst_insP2", 0.5f);
                setParam(p.apvts, "mst_ins2Char", 10.0f); setParam(p.apvts, "mst_ins2P1", 0.55f); setParam(p.apvts, "mst_ins2P2", 0.4f);
            };
            d.timed = { setAt(72000, 0, "drvChar", 4) };
            defs.push_back(d);
        }

        // Tempo moves while playing: the gate is beat-locked and the delay is tempo-synced.
        {
            Def d; d.name = "tempo-change-midplay"; d.numStems = 1; d.lengthFrames = 144000;
            d.notes = "Gated voice with a synced delay send; tempo 120 -> 90 (1 s) -> 160 (2 s).";
            d.author = [](PluginProcessor& p)
            {
                gate(p, 0, 1, { { 0, 2, 0.0f, 0.0f, 0.0f, false }, { 4, 2, 0.2f, 0.0f, 0.0f, false },
                                { 8, 2, 0.0f, 0.0f, 0.0f, false }, { 12, 3, 0.4f, 0.0f, 0.0f, false } });
                setVoice(p.apvts, 0, "o1_wt", 2); setVoice(p.apvts, 0, "flt_cut", 3000);
                setParam(p.apvts, "ch0_sendDly", 0.5f); setParam(p.apvts, "ret_dly_lvl", 0.8f); setParam(p.apvts, "dly_fb", 0.5f);
            };
            d.timed = { { 48000, "tempo 90",  [](OwnedProcessor&, mu_golden::MockHost& h) { h.bpm = 90.0; } },
                        { 96000, "tempo 160", [](OwnedProcessor&, mu_golden::MockHost& h) { h.bpm = 160.0; } } };
            defs.push_back(d);
        }

        // The host stops, starts, locates, and the 64-beat wrap of the bounded beat space is crossed.
        {
            Def d; d.name = "transport-stop-start-wrap64"; d.numStems = 1; d.lengthFrames = 288000; d.startBeat = 62.0;
            d.notes = "Starts at beat 62 so the 64-beat wrap falls at 1 s; stop at 2.5 s, start at 3 s, locate to beat 127.75 (crosses 128) at 3.5 s, to beat 300.25 at 4.5 s, to beat 1000000.25 at 5.25 s.";
            d.author = [](PluginProcessor& p)
            {
                gate(p, 0, 1, { { 0, 3, 0.0f, 0.0f, 0.0f, false }, { 6, 2, 0.3f, 0.0f, 0.0f, false },
                                { 10, 2, 0.0f, 0.0f, 0.0f, false }, { 14, 2, 0.5f, 0.0f, 0.0f, false } });
                setVoice(p.apvts, 0, "o1_wt", 1); setVoice(p.apvts, 0, "flt_cut", 3000);
            };
            d.timed = { { 120000, "host stops",  [](OwnedProcessor&, mu_golden::MockHost& h) { h.playing = false; } },
                        { 144000, "host starts", [](OwnedProcessor&, mu_golden::MockHost& h) { h.playing = true; } },
                        { 168000, "locate to beat 127.75",   [](OwnedProcessor&, mu_golden::MockHost& h) { h.ppq = 127.75; } },
                        { 216000, "locate to beat 300.25",   [](OwnedProcessor&, mu_golden::MockHost& h) { h.ppq = 300.25; } },
                        { 252000, "locate to beat 1000000.25", [](OwnedProcessor&, mu_golden::MockHost& h) { h.ppq = 1000000.25; } } };
            defs.push_back(d);
        }

        // A count-reducing swap while playing retires the dropped voices with a 25 ms fade, then a regrow;
        // the last steps shrink, regrow and shrink again inside one fade (the stale-tail case).
        {
            Def d; d.name = "retire-regrow"; d.numStems = 3; d.lengthFrames = 192000; d.startBeat = 3.0;
            d.isolateStemsByMute = true;   // the mixer ignores a solo on a dropped strip, which would let the others through
            d.notes = "Three drones (voices 1 and 2 through Karplus). A staged one-voice preset commits at the voice-0 loop (beat 4, 0.5 s) and retires voices 1 and 2; a restore with three voices regrows (1 s); restores with two, three and two voices again, the first two inside one fade (1.26 s), drop voice 2 twice.";
            d.drainDeferredWorkEveryBlock = true;
            d.author = [](PluginProcessor& p)
            {
                mu_golden_rig::growTo(p, 3);
                for (int v = 0; v < 3; ++v) { openGate(p.apvts, v); setVoice(p.apvts, v, "o1_wt", 1); }
                gate(p, 0, 1, {});   // a one-bar voice-0 pattern: the full-preset swap boundary is 4 beats
                setVoice(p.apvts, 1, "o1_oct", -1); setVoice(p.apvts, 2, "o1_oct", 1);
                for (int v : { 1, 2 }) { setVoice(p.apvts, v, "drvChar", 11); setVoice(p.apvts, v, "insP1", 0.6f); setVoice(p.apvts, v, "insP2", 0.8f); }
            };
            d.timed = {
                stageAt(6144, 1),
                restoreAt(48000, 3),
                setAt(54000, 2, "o1_oct", 2),
                restoreAt(60416, 2),
                restoreAt(60928, 3),
                setAt(66000, 2, "o1_oct", -1),
                restoreAt(72192, 2) };
            defs.push_back(d);
        }

        return defs;
    }

    juce::String midiText(const juce::MidiMessage& m)
    {
        return m.isNoteOn() ? "noteOn " + juce::String(m.getNoteNumber()) : "noteOff " + juce::String(m.getNoteNumber());
    }

    // scenario.json, written from the Def so it cannot drift from what runs.
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
        s << "  \"drainDeferredWorkEveryBlock\": " << (d.drainDeferredWorkEveryBlock ? "true" : "false") << ",\n";
        s << "  \"isolateStemsByMute\": " << (d.isolateStemsByMute ? "true" : "false") << ",\n";
        s << "  \"schedules\": [";
        for (size_t i = 0; i < d.schedules.size(); ++i)
        {
            s << (i ? ", " : "") << "{ \"name\": \"" << d.schedules[i].name << "\", \"blocks\": [";
            for (size_t k = 0; k < d.schedules[i].blocks.size(); ++k)
                s << (k ? ", " : "") << d.schedules[i].blocks[k];
            s << "] }";
        }
        s << "],\n";
        s << "  \"timedActions\": [";
        for (size_t i = 0; i < d.timed.size(); ++i)
            s << (i ? ", " : "") << "{ \"frame\": " << d.timed[i].frame << ", \"label\": \"" << d.timed[i].label << "\" }";
        s << "],\n";
        s << "  \"midiByBlock\": [";
        for (size_t i = 0; i < d.midi.size(); ++i)
            s << (i ? ", " : "") << "{ \"block\": " << d.midi[i].block << ", \"offset\": " << d.midi[i].sampleOffset
              << ", \"message\": \"" << midiText(d.midi[i].message) << "\" }";
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

    // The timed actions as harness events: one poller per block that could occur. The poller runs every
    // action whose frame has been reached (once each, recorded on the processor) and then the deferred
    // work poll, so a staged swap commits in the block after the audio thread flagged it.
    std::vector<mu_golden::ScriptedEvent> pollers(const Def& d)
    {
        struct Plan { std::vector<Timed> timed; bool drain; };
        const auto plan = std::make_shared<const Plan>(Plan { d.timed, d.drainDeferredWorkEveryBlock });
        std::vector<mu_golden::ScriptedEvent> events;
        if (plan->timed.empty() && ! plan->drain) return events;

        const auto poll = [plan](mu_golden::ScriptContext& c)
        {
            auto& o = static_cast<OwnedProcessor&>(c.proc);
            if (o.fired.size() != plan->timed.size()) o.fired.assign(plan->timed.size(), false);
            for (size_t i = 0; i < plan->timed.size(); ++i)
                if (! o.fired[i] && c.frame >= plan->timed[i].frame)
                {
                    o.fired[i] = true;
                    plan->timed[i].run(o, c.host);
                }
            if (plan->drain) o.drainDeferredWork();
        };
        // The 1-sample blocks of the ragged schedule come eight to the 1803 frames, so a block per 100
        // frames is more than any schedule produces.
        const int maxBlocks = (int) (d.lengthFrames / 100) + 16;
        for (int b = 0; b < maxBlocks; ++b) events.push_back({ b, "timed actions", poll });
        return events;
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
        sc.events = pollers(d);
        sc.midi = d.midi;
        if (d.isolateStemsByMute)
        {
            // After the harness's solo: clear every solo and mute all strips but the stem's, so a voice
            // that is dropped mid-render (its retire tail included) is still heard alone.
            sc.stemSetup = [](ProcessorBase& pb, int stem)
            {
                for (int c = 0; c < mu_limits::kMaxLayers; ++c)
                {
                    setParam(pb.apvts, "ch" + juce::String(c) + "_solo", 0.0f);
                    setParam(pb.apvts, "ch" + juce::String(c) + "_mute", c == stem ? 0.0f : 1.0f);
                }
            };
        }
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

struct MuTantGoldenRenderTests : juce::UnitTest
{
    MuTantGoldenRenderTests() : juce::UnitTest("mu-Tant golden renders", "mu-tant-golden") {}

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
            const bool skipThis = store.updateMode && store.scenarioFilter != "mu-tant/" + d.name;
            if (skipThis) continue;   // update mode renders one scenario only (a mu-Tant render is slow)

            beginTest(d.name + ": inputs exist");
            expect(scenarioDir(d.name).getChildFile("state.xml").existsAsFile(), "state.xml missing; run with MU_GOLDEN_AUTHOR_STATE=1");
            if (! scenarioDir(d.name).getChildFile("state.xml").existsAsFile()) continue;

            if (! store.updateMode)
            {
                beginTest(d.name + ": renders the same twice in one process");
                const auto rep = store.verifyRepeatable(sc, factory);
                expect(rep.ok, rep.message);
            }

            beginTest(d.name + ": matches the golden");
            allocProbe.setScenario(d.name);
            const auto out = store.verify("mu-tant", sc, factory);
            if (out.skipped) continue;
            expect(out.ok, out.message);
            if (out.message.isNotEmpty()) logMessage(out.message);

            beginTest(d.name + ": no heap allocation or free inside processBlock");
            reportAllocations(d.name, allocProbe.take());
        }
    }
};

static MuTantGoldenRenderTests muTantGoldenRenderTests;
