# Tests

Three test layers run for every release:

| Layer | Mechanism | Run via |
|---|---|---|
| Compile/data invariants | JUCE `juce::UnitTest` subclasses | See per-product sections below |
| Audio behaviour | Python pipeline rendering presets via the standalone's `--render` CLI, then asserting on the WAV | `python tests/scripts/run-listening-tests.py --config Release` |
| Manual smoke | 25-step walkthrough of the standalone UI | [docs/mu-clid/TestPlan.md](docs/mu-clid/TestPlan.md) |

The audio pipeline is the one most often forgotten — wire it into the same shell that runs the C++ unit tests on Release builds.

---

## Listening tests (audio behaviour)

Each `tests/expectations/T<N>.json` pairs a `.muRhythm` preset with a list of pass/fail assertions. The orchestrator renders the preset headlessly and runs `analyse.py` against the resulting WAV. Subjective sound quality still needs ears — these tests catch the OBJECTIVE class of regression (audio cut where it shouldn't, ringing where it shouldn't, expected frequency content missing).

**Status key:** ✅ Pass &nbsp;|&nbsp; ❌ Fail &nbsp;|&nbsp; 🔵 To run &nbsp;|&nbsp; 🟡 Blocked

| # | Test | Status | Verified Build |
|---|---|---|---|
| T11 | **[#627 — FX tail survives hot-swap]** Load a preset with strong filter resonance (e.g. ladder LP @ res 85+, cutoff 800 Hz, decay 4 s) AND a long sustained sample (pad / drone). Play, wait for a hit + the filter to ring. Click a different preset (e.g. a clean kick). **Expected**: the old engine's sample + amp env tail continues AND the filter resonance keeps ringing as the env decays; both fade together as the env hits idle. Pre-fix #627 the filter ring was cut instantly at swap. **Failure**: filter ring still cut, OR retired engine's filter bleeds INTO the new active rhythm's audio (= the #416 "overlay" bug returning). | ✅ Pass | 608 |
| T12 | **[#627 — Comb-filter / Karplus / reverb-style insert FX tails ring through env release on the ACTIVE engine]** Load a preset with Karplus insert (algo 11) with high feedback (Feedback ≈ 80 %), short decay (300 ms). Trigger a hit. **Expected**: Karplus ringing extends past the sample length, fading naturally with the envelope's release. Pre-fix this was already mostly OK on the active engine but had latent issues — confirm no regression. Repeat with a comb-filter preset (filter type Comb+ / Comb-) at high resonance. **Failure**: comb feedback cut at sample-end, OR continues at full level after env idle. | ✅ Pass | 609 |
| TS_swap | **[Full-preset hot-swap — loop-point + prestage + tail-out]** Play TS1 (steady kick), load the EMPTY TS2 mid-play (`--swap-at 2.0s`). **Expected**: the swap defers to the next loop point (rhythm 0's loop when free-running, else the master loop), the outgoing kick voice rings out its tail across the boundary (retire-then-swap), then silence (TS2 makes no sound). One assertion catches all three regressions: swap never commits (free-running hang), swap hard-cuts the tail, or TS1 keeps triggering after the swap. Presets `tests/presets/TS1.muClid` + `TS2.muClid`. | ✅ Pass | 626 |
| T13 | **[#627 — Polyphonic retrigger behaviour change]** Engine-level amp env means rapid retriggers on overlapping voices now share the env state (no per-voice independent release tails). Trigger overlapping hits on a percussive preset with decay 1 s — listen for any subjectively-worse retrigger feel vs pre-#627. **Expected (drum patterns)**: no perceptible difference — each hit gets a clean retrigger. **Pad patterns**: enable Pattern Legato (#419) so tied hits keep the env going; without legato, expect the env to retrigger on each hit (= "pulsed" pad feel). Confirm pattern legato compensates. **Failure**: drum patterns sound wrong, OR pattern-legato pad doesn't behave smoothly. | ✅ Pass | 616 |

Test presets ship under `<contentDir>/Rhythms/T<N>.muRhythm` (category `test`) so they appear in the regular preset dropdown for manual auditioning.

See [tests/README.md](tests/README.md) for the full pipeline overview — render flags, JSON schema, metric catalogue, and the "adding a new listening test" recipe.

---

## C++ unit tests — μ-Clid

Console executable that runs every `juce::UnitTest` defined in [mu-clid/Source/Tests/](mu-clid/Source/Tests/). **Excluded from ALL build** (deliberate: Release builds deploy to testers and the 140-test suite linking `juce_audio_processors` adds significant compile time). Build on demand:

```
cmake --build build --target mu-clid-tests --config Release && build/mu-clid/mu-clid-tests_artefacts/Release/mu-clid-tests.exe
```

| Coverage | File |
|---|---|
| Per-rhythm param round-trip via APVTS / preset XML | `RhythmParamRoundTripTests.cpp` |
| Per-rhythm param boundary clamps | `RhythmParamBoundaryTests.cpp` |
| Global preset param defs | `GlobalParamDefsTests.cpp` |
| Kinded property serialisation | `KindedPropertyRoundTripTests.cpp` |
| Modulator (CS + assignment) round-trip | `ModulatorSerialiseTests.cpp` |
| Algorithm name table invariants (sizes, ASCII names) | `AlgorithmNameTableTests.cpp` |
| Insert slot config table invariants | `InsertAlgoTableTests.cpp` |
| HitGenerator pattern correctness | `HitGeneratorTests.cpp` |
| ModulationMatrix process semantics | `ModulationMatrixTests.cpp` |
| APVTS layout sanity (every ID + range + default) | `ApvtsLayoutTests.cpp` |
| UI ↔ APVTS scaling consistency | `UIScalingConsistencyTests.cpp` |
| Insert DSP smoke (no NaN, sensible output range) | `InsertDSPSmokeTests.cpp` |
| Filter DSP smoke | `FilterDSPSmokeTests.cpp` |
| Send-FX DSP smoke | `SendFXSmokeTests.cpp` |
| Preset XML round-trip | `PresetXMLRoundTripTests.cpp` |
| Preset migration (v0/v1 → v2 gate) | `PresetMigrationTests.cpp` |
| MIDI full-preset map | `MidiFullPresetMapTests.cpp` |
| Hot-swap boundary timing | `HotSwapBoundaryTests.cpp` |
| Modulation skew (proportion ↔ display) | `ModulationSkewTests.cpp` |

**Current status:** 140/140 tests pass at v1.0.708.

---

## C++ unit tests — μ-Tant

Built as part of **ALL_BUILD** every Debug build (mu-tant is local-only and the suite is small enough that the compile cost is negligible). Build command:

```
cmake --build build --target mu-tant-tests --config Debug && build/mu-tant/mu-tant-tests_artefacts/Debug/mu-tant-tests.exe
```

| Coverage | File |
|---|---|
| Scale-quantised pitch math + wavetable oscillator frequency | `SynthDSPTests.cpp` |
| Voice engine (noise, FM/AM/Ring/Sync, level, additive sum) | `SynthVoiceTests.cpp` |
| Gate envelope shape (split, bends, gap, reverse) | `GatePatternTests.cpp` |
| Gate envelope playback rules (probability, loopMask/loopM) | `GatePatternTests.cpp` |
| Gate pattern editing (addEnvelope, removeEnvelope, mergeRange) | `GatePatternTests.cpp` |
| Block-level gater (bypass/stopped/playing states) | `GateStageTests.cpp` |
| Gate editor interactions (pencil, eraser, reverse tools; mergeRange data model) | `GatingDesignerTests.cpp` |
| Modulator (ControlSequence + ModulationMatrix) | `ModulatorTests.cpp` |
| Insert stage DSP smoke (None passthrough, SoftClip) | `InsertStageTests.cpp` |
| Scales table correctness | `ScalesTests.cpp` |
| ControlSequence timing/retrigger | `ControlSequenceTests.cpp` |
| Modulator + gate persistence round-trip | `MuTantPersistTests.cpp` |
| APVTS layout sanity (every `v{N}_*` / `ch{N}_*` param) | `ApvtsLayoutTests.cpp` |
| Send-FX DSP smoke | `SendFXSmokeTests.cpp` |
| Preset serialisation round-trip (envelopes, filter/pitch gate, copyDataFrom) | `PresetRoundTripTests.cpp` |

**Current status:** 112/112 tests pass at v1.0.723.

---

## C++ unit tests — μ-On

Built as part of **ALL_BUILD** every build (mirrors mu-tant-tests — local-only product, small suite). Build command:

```
cmake --build build --target mu-on-tests --config Debug && build/mu-on/mu-on-tests_artefacts/Debug/mu-on-tests.exe
```

| Coverage | File |
|---|---|
| APVTS layout sanity (every `ch{N}_*` / engine / sequencer param ID + range + default) | `ApvtsLayoutTests.cpp` |
| Sequencer beat→step mapping, step-edge firing, swing, accent velocity, `<Pattern>` serialise | `SequencerTests.cpp` |
| Engine smoke — Kick/Hat make sound + decay to silence | `EngineTests.cpp` |
| Per-lane modulation — `.prop` proportion-space scale (1.0), lane-scoped dest tables | `ModulationTests.cpp` |
| **Per-lane dest-table ↔ engine `setParams` order integrity guard** (pins each lane's full id order) | `ModulationTests.cpp` |
| Modulator serialise round-trip drops foreign-lane destinations | `ModulationTests.cpp` |

**Current status:** 15/15 tests pass at v1.0.812.

---

## Manual smoke test plan

See [docs/mu-clid/TestPlan.md](docs/mu-clid/TestPlan.md) — 25 atomic UI tests, ordered so each builds on the previous. Run end-to-end against a Release build. Investigate any deviation before proceeding.

### Manual hardware / mu-link sync tests

These need real audio + MIDI hardware (and/or multiple running apps), so they can't run in the headless pipeline. Same status key as the listening-tests table.

| # | Test | Status | Verified Build |
|---|---|---|---|
| MS1 | **[#878 — mu-link MIDI-clock sync]** With an external MIDI-clock source driving real outboard gear, verify mu sync stays locked **both ways**: (a) mu-clid slaved **through mu-link** (the bus carries clock + audio to the shared device), and (b) mu-clid **standalone on its own** (direct external-MIDI slave, no mu-link present). **Expected**: transport/tempo follow the master with no audible drift; standalone reverts cleanly to its own device when mu-link isn't running. | ✅ Pass | 793 |

---

## Adding a regression test

1. **Decide the layer.** Compile-time / data invariant → C++ unit test. Audio behaviour → listening test. UI feel → smoke-test step.
2. **Listening tests:** follow the 7-step recipe in [tests/README.md](tests/README.md#adding-a-new-listening-test).
3. **C++ unit tests:** add a `Source/Tests/<Name>Tests.cpp` deriving from `juce::UnitTest`, add the file to the product's test target (`mu-clid-tests` / `mu-tant-tests` / `mu-on-tests`) in the respective `CMakeLists.txt`.
4. **Always** add a row above so the test isn't an orphan. Use the same status keys as the listening-tests table for consistency.
