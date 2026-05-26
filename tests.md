# Tests

Three test layers run for every release:

| Layer | Mechanism | Run via |
|---|---|---|
| Compile/data invariants | JUCE `juce::UnitTest` subclasses in [mu-clid/Source/Tests/](mu-clid/Source/Tests/) | `cmake --build build --target mu-clid-tests --config Release && build/mu-clid/mu-clid-tests_artefacts/Release/mu-clid-tests.exe` |
| Audio behaviour | Python pipeline rendering presets via the standalone's `--render` CLI, then asserting on the WAV | `python tests/scripts/run-listening-tests.py --config Release` |
| Manual smoke | 25-step walkthrough of the standalone UI | [docs/TestPlan.md](docs/TestPlan.md) |

The audio pipeline is the one most often forgotten — wire it into the same shell that runs the C++ unit tests on Release builds.

---

## Listening tests (audio behaviour)

Each `tests/expectations/T<N>.json` pairs a `.muRhyth` preset with a list of pass/fail assertions. The orchestrator renders the preset headlessly and runs `analyse.py` against the resulting WAV. Subjective sound quality still needs ears — these tests catch the OBJECTIVE class of regression (audio cut where it shouldn't, ringing where it shouldn't, expected frequency content missing).

**Status key:** ✅ Pass &nbsp;|&nbsp; ❌ Fail &nbsp;|&nbsp; 🔵 To run &nbsp;|&nbsp; 🟡 Blocked

| # | Test | Status | Verified Build |
|---|---|---|---|
| T11 | **[#627 — FX tail survives hot-swap]** Load a preset with strong filter resonance (e.g. ladder LP @ res 85+, cutoff 800 Hz, decay 4 s) AND a long sustained sample (pad / drone). Play, wait for a hit + the filter to ring. Click a different preset (e.g. a clean kick). **Expected**: the old engine's sample + amp env tail continues AND the filter resonance keeps ringing as the env decays; both fade together as the env hits idle. Pre-fix #627 the filter ring was cut instantly at swap. **Failure**: filter ring still cut, OR retired engine's filter bleeds INTO the new active rhythm's audio (= the #416 "overlay" bug returning). | ✅ Pass | 608 |
| T12 | **[#627 — Comb-filter / Karplus / reverb-style insert FX tails ring through env release on the ACTIVE engine]** Load a preset with Karplus insert (algo 11) with high feedback (Feedback ≈ 80 %), short decay (300 ms). Trigger a hit. **Expected**: Karplus ringing extends past the sample length, fading naturally with the envelope's release. Pre-fix this was already mostly OK on the active engine but had latent issues — confirm no regression. Repeat with a comb-filter preset (filter type Comb+ / Comb-) at high resonance. **Failure**: comb feedback cut at sample-end, OR continues at full level after env idle. | ✅ Pass | 609 |
| T13 | **[#627 — Polyphonic retrigger behaviour change]** Engine-level amp env means rapid retriggers on overlapping voices now share the env state (no per-voice independent release tails). Trigger overlapping hits on a percussive preset with decay 1 s — listen for any subjectively-worse retrigger feel vs pre-#627. **Expected (drum patterns)**: no perceptible difference — each hit gets a clean retrigger. **Pad patterns**: enable Pattern Legato (#419) so tied hits keep the env going; without legato, expect the env to retrigger on each hit (= "pulsed" pad feel). Confirm pattern legato compensates. **Failure**: drum patterns sound wrong, OR pattern-legato pad doesn't behave smoothly. | ✅ Pass | 616 |

Test presets ship under `<contentDir>/Rhythms/T<N>.muRhyth` (category `test`) so they appear in the regular preset dropdown for manual auditioning.

See [tests/README.md](tests/README.md) for the full pipeline overview — render flags, JSON schema, metric catalogue, and the "adding a new listening test" recipe.

---

## C++ unit tests (data invariants + DSP smoke)

Console executable that runs every `juce::UnitTest` defined in [mu-clid/Source/Tests/](mu-clid/Source/Tests/). Excluded from the default ALL build — only compiled when asked, so day-to-day plugin iteration isn't slowed by test compilation.

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

**Current status:** 119/119 tests pass at v1.0.614.

---

## Manual smoke test plan

See [docs/TestPlan.md](docs/TestPlan.md) — 25 atomic UI tests, ordered so each builds on the previous. Run end-to-end against a Release build. Investigate any deviation before proceeding.

---

## Adding a regression test

1. **Decide the layer.** Compile-time / data invariant → C++ unit test. Audio behaviour → listening test. UI feel → smoke-test step.
2. **Listening tests:** follow the 7-step recipe in [tests/README.md](tests/README.md#adding-a-new-listening-test).
3. **C++ unit tests:** add a `Source/Tests/<Name>Tests.cpp` deriving from `juce::UnitTest`, add the file to `mu-clid-tests` in `mu-clid/CMakeLists.txt`.
4. **Always** add a row above so the test isn't an orphan. Use the same status keys as the listening-tests table for consistency.
