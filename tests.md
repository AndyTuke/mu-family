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
| TONI_swap | **[μ-Toni full-preset hot-swap — shared BarLineSwapper]** Factory state plays; the silent full preset `tests/presets/TONI_silent.muToni` (master level 0) is loaded at 0.5 s. **Expected**: still playing until the 2.0 s bar line, then silent — the swap staged and committed at the bar, not at once. | ✅ Pass | 1117 |
| TONI_slot_swap | **[μ-Toni layer-preset hot-swap]** `tests/presets/TONI_mute_layer.muArp` (both oscillator levels at minimum) staged onto layer 0 at 0.5 s. **Expected**: layer 0 keeps playing until the 2.0 s bar line, then silent. | ✅ Pass | 1117 |
| ON_swap | **[μ-On full-preset hot-swap — shared BarLineSwapper]** The factory groove plays; the silent full preset `tests/presets/ON_silent.muOn` is loaded at 0.5 s. **Expected**: groove continues to the 2.0 s pattern wrap, then silent. | ✅ Pass | 1117 |
| TANT_preset | **[μ-Tant preset render]** Factory full preset `Presets/Acid Cellar.muTant` (content folder) rendered with the transport playing. **Expected**: its gated voices are audible (catches a preset / transport that silently fails to load headlessly). | ✅ Pass | 1117 |
| TONI_roundtrip | **[μ-Toni composed-state round trip]** `tests/presets/TONI_legacy.muToni` (a pre-format-2 APVTS dump: dark layer-1 filter, layers 2-4 muted, a layer-1 modulator). **Expected**: the old values take effect (centroid well below the factory ~2550 Hz), and re-renders from the saved full preset and from the saved host session are sample-identical. | ✅ Pass | 1130 |
| ON_roundtrip | **[μ-On composed-state round trip]** `tests/presets/ON_legacy.muOn` (pre-format-2: the grid as one root `<Pattern>` with only the hat stepping, the Rumble envelope at the root). **Expected**: hat-only level (~-33 dB vs the factory ~-12 dB), and preset + session re-renders sample-identical. | ✅ Pass | 1130 |
| TANT_roundtrip | **[μ-Tant composed-state round trip]** Factory preset `Presets/To and Fro.muTant` (saved before the X-Mod redesign). **Expected**: audible after the legacy rebuild + X-Mod migration, and preset + session re-renders sample-identical. | ✅ Pass | 1130 |
| CLID_roundtrip | **[μ-Clid preset / session round trip]** `tests/presets/A3_filter.muClid` through the shared rhythm prepare + install. **Expected**: preset and session (`.muClid` tree + `<SessionParams>`) re-renders sample-identical. | ✅ Pass | 1130 |
| TONI_host_lock | **[Family transport rule — host position]** mu-Toni rendered as if in a playing host whose timeline starts at beat 0.125 (`--host-start-beat`). **Expected**: silent until beat 0.25, then the first arp note — the steps sit on the host's beat grid (the old free-running arp fired at once). | ✅ Pass | 1145 |
| CLID_midi_clock / TANT_midi_clock / TONI_midi_clock / ON_midi_clock | **[MIDI clock slave — every product]** External clock at 120 BPM from t=0 with +/-24 samples of jitter, Start at 0.5 s, Messages = both, own Play on (`--midi-clock`). **Expected**: silent until the Start (clock pulses while stopped don't play), then the pattern plays. | ✅ Pass | 1161 |
| mutant_drone | **[μ-Tant audio smoke]** No preset: voice 0's gate bypassed, transport stopped — the raw oscillator drone must be audible (content-independent; used by mac-validate). | ✅ Pass | 1117 |

Test presets ship under `<contentDir>/Rhythms/T<N>.muRhythm` (category `test`) so they appear in the regular preset dropdown for manual auditioning.

See [tests/README.md](tests/README.md) for the full pipeline overview — render flags, JSON schema, metric catalogue, and the "adding a new listening test" recipe.

---

## C++ unit tests — μ-Clid

Console executable that runs every `juce::UnitTest` defined in [mu-clid/Source/Tests/](mu-clid/Source/Tests/). **Excluded from ALL build** (deliberate: the 140-test suite linking `juce_audio_processors` adds significant compile time). Build on demand:

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
| Insert slot config table invariants | `InsertAlgoTableTests.cpp` |
| HitGenerator pattern correctness | `HitGeneratorTests.cpp` |
| APVTS layout sanity (every ID + range + default) | `ApvtsLayoutTests.cpp` |
| Preset XML round-trip | `PresetXMLRoundTripTests.cpp` |
| Preset migration (v0/v1 → v2 gate) | `PresetMigrationTests.cpp` |
| Staged hot-swaps vs apply-at-once paths on the real processor (transport state set directly, no audio thread): `takeStagedFullPreset` returns a staged full preset before and after a boundary flagged it, and `processSwaps` then commits nothing; `loadPreset` on an unparseable file returns false, reports the error and publishes no preset name (a good file does); `setStateInformation` with a MuClidPreset session and with a legacy-format tree each cancel a staged per-rhythm swap and a staged full preset; a stopped `loadPreset` cancels both. The take-back race inside `loadPreset` is not testable deterministically and has no test. **Pass (Debug, build 1409; 8 results, mu-clid-tests 183 results, 0 failures)** | `HotSwapCancelTests.cpp` |
| Shared per-preset swap timing (`mu_hotswap::SwapTiming`, `mstr_swap`): saved integers 0/1/2 and out-of-range reads as OnMasterLoop; layer swap on the master wrap, on the layer's own loop, free-running fallback to the layer loop, OnBar on the bar line only; layer-index sweep 0..7 and the full-preset sentinel / out-of-range index never fire; full-preset swap (master loop, free-running layer 0, OnBar); bar wrap across a block. Replaces the deleted mu-Clid `HotSwapBoundaryTests.cpp` (all five of its cases are covered here). **Pass (Debug, build 1409; core 297 results, 0 failures)** | `mu-core/Tests/SwapTimingTests.cpp` |
| Performance mode live state (`LiveStateTests.cpp`, fake callbacks): the mode needs a loaded performance and a supporting product, and every other action needs the mode; a bare action toggles; item stage / queue / commit; same-item press is a no-op or cancels the queued switch; stopped transport applies at once; failed load, unloadable and out-of-range items refused; a foreign preset staged over the queue clears it and its commit leaves no item current; early apply settles at reconcile; Prev / Next wrap, skip unusable items, step from the queued item, refuse with no other item; SelectLayer absolute and bounded; layer clamp after a commit; leaving the mode cancels; listeners; session persistence (path, item, mode; not queue or selection; unsupported build never comes back locked; missing file restores nothing). Plus `MidiControlTests`: a performance item index round trips through the MIDI map. **Pass (Debug, build 1409; mu-core 313, 0 failures)** | `mu-core/Tests/LiveStateTests.cpp`, `MidiControlTests.cpp` |
| Fader thumb geometry (`mu_ui::fader`, pure maths, 8 tests): cap size nominal and clamped to the cross axis at 1.6 aspect, thumb radius, groove and accent-fill rectangles for vertical and horizontal faders. Painting itself is not covered (looked at, not yet seen per the stage commit; manual check). **Pass (Debug, build 1409; mu-core 344, 0 failures)** | `mu-core/Tests/FaderThumbGeometryTests.cpp` |
| `BipolarSliderRow` modulation colour: the depth bar and curve knob both carry `knobModulation` as their fill colour (the Slider default is the Euclidean purple). **Pass (Debug, build 1409; mu-core 344, 0 failures)** | `mu-core/Tests/BipolarSliderRowTests.cpp` |
| Performance mode shell step 3 fixes: `StepIndicator::splitText` drops a ghost narrower than the minimum (no stub) and keeps one that fits; the exit-hold release and completion both take the 'Hold to leave' status line down (`|` line). Queued-line clear and indicator cache-per-revision have no direct test (gap, low risk, visual). **Pass (Debug, build 1409; mu-core 344, 0 failures)** | `mu-core/Tests/PerformanceModeShellTests.cpp` |
| Performance chooser (`PerformanceChooserTests.cpp`): model (add / replace / move / remove rows, missing and foreign-product status, save / rename / delete / use, sorted library); layout at both sizes; shell wiring (Performances button, Perf toggle and Ctrl+P open it, starts from the list in use, entering the mode closes it); pick round trip, browser Close cancels the pick and returns, a browser hidden mid-pick ends pick mode; Use on a saved / unsaved / changed / empty list; discard-changes, Delete and Overwrite confirms; Use on an unsaved list with a taken name runs overwrite then save then use, and declining it uses nothing and a later plain Save does not use; a backend whose `getFullPresetDir` differs from `getPresetsDir` picks, uses and resolves items. Gaps: rename does not check the old-file delete result (low risk, no seam to fail the delete); painting is by eye only. **Pass (Debug, build 1409; mu-core 376 results, 0 failures)** | `mu-core/Tests/PerformanceChooserTests.cpp`, `TestShellBackend.h` |
| Performance mode on the real processor (mu-Toni seam, stand-in for the standalone wrapper): a two-preset `PerformanceFile` of saved full presets, the mode entered, `PerformanceItem` and `PerformanceNext` load the presets with the transport stopped (applied at once, parameter value live), a missing-file and an empty item are refused with nothing changed, Next wraps past them, `saveLiveState` / `restoreLiveState` bring the gig back on a second instance, a plugin instance refuses the mode. Covers the real `canLoad` (`PerformanceFile` status), `stageItem` and Applied detection. Not covered: the queued path (running transport) on the real processor, covered with fakes only. **Pass (Debug, build 1409; mu-Toni 0 failures)** | `mu-toni/Source/Tests/ProcessorSeamTests.cpp` |
| `mstr_swap` exists in every product with its default (OnMasterLoop in mu-Clid / mu-Tant, OnBar in mu-Toni / mu-On). **Pass (Debug, build 1409; mu-Clid 183, mu-Tant, 0 failures; mu-Toni, mu-On per the stage commit)** | `<product>/Source/Tests/ProcessorSeamTests.cpp` |
| mu-Tant hot-swap boundary: `LayerSwapper` driven by `swapLoopBeats` (wrap detection incl. non-divisor lengths, stop edge, supersede, per-voice vs full) and, for OnBar, a voice swap and a full preset staged then committed on the 4-beat bar line (not the voice's 8-beat loop nor a 16-beat master loop), both free-running and with a master loop, plus an unlocked pattern length. **Pass (Debug, build 1409; mu-Tant 170 results, 0 failures)** | `mu-tant/Source/Tests/HotSwapBoundaryTests.cpp` |
| Transport position into the engine (`SwapCommitTimingTests.cpp`): a host position that creeps backward 0.002 beat per block is held for under a step then followed (hold bounded, never forward while the host reverses, afterwards each step plays its own pattern slot with no drift or repeat); a jittery ramp (up to 0.04 beat either way) keeps a monotonic engine position, no wrap-detector reset beyond the transport start, one trigger per step reached; the modulation recompute (`tryUpdatePatternFromModulation`) pads steps 1 and 2 with 0 and 1 hits to at least 2 steps and the walk carries on. Mutation checks: removing the hold, or making it unbounded, or dropping the minimum-length pad in the recompute, each fails these tests | `SwapCommitTimingTests.cpp` |
| Modulation skew (proportion ↔ display) | `ModulationSkewTests.cpp` |
| No demo caps (unlicensed is fully functional): 8 layers, 64-step rhythms and 8-rhythm full-preset / per-layer files survive save/load as XML text; limits stay 8 / 64. Data-layer only; the processor-level numRhythms clamp is not reachable from this target. **Pass (build 1255)** | `NoDemoCapsTests.cpp` |
| Layer structure, held-note release: a reset releases a held sample note within the release time without allocating; a tied hit 20 blocks into the release adds no attack (level at most 2.1 times the pre-tie peak, since the tied hit adds a second voice under the same envelope) and the release still reaches silence, which fails if the envelope retriggered (it would hold at sustain); the strip's left / right peaks follow the pan (hard left leaves the right side silent, a centred mono source gives equal sides, the combined peak is the larger side) | `ClidLayerStructureTests.cpp` |

**Current status:** 140/140 tests pass at v1.0.708.  Layer-structure suite re-run: 179 results, 0 failures (Debug, build 1409), including the tied-mid-release and stereo-peak tests.

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
| No demo caps (unlicensed is fully functional): gates longer than 16 cells, 8 voices x 3 long gates and full-preset / per-layer files survive save/load as XML text; limit stays 8. Data-layer only; the processor-level numVoices clamp is not reachable from this target. **Pass (build 1255)** | `NoDemoCapsTests.cpp` |

**Current status:** 112/112 tests pass at v1.0.723.

---

## C++ unit tests — mu-core (shared)

Tests of shared mu-core behaviour live in `mu-core/Tests` and are built once as `mu-core-tests` (part of **ALL_BUILD**). They link no product: a fake `ProcessorCore` (or, for UI pages, a fake `ProcessorBase`) stands in for a processor. Build command:

```
cmake --build build --target mu-core-tests --config Debug && build/mu-core/Tests/mu-core-tests_artefacts/Debug/mu-core-tests.exe
```

| Coverage | File | Status |
|---|---|---|
| Host transport publishing: defaults, play + ppq, no ppq, meter + bar start (clamped to 255), null playhead reset, bypassed block still publishes | `mu-core/Tests/HostTransportPublishTests.cpp` | Pass (10 results, 0 failures, build 1243) |
| Layer access: valid index, out of range → nullptr, const overload, name + colour via the base accessors | `mu-core/Tests/LayerAccessTests.cpp` | Pass (build 1243) |
| Support prompt policy: supporter rules, when the prompt shows, once per process, "Don't show again" saved-flag round trip | `mu-core/Tests/SupportPromptPolicyTests.cpp` | Pass (14 results, 0 failures, build 1245) |
| Settings page fit (`StandardSettingsOverlay`, two-column): at 1088x814 and 1088x774 (banner allowance), UI scale 1.0 and 1.25, standalone and plugin wrapper, with and without a mu-Clid-style Locations group (two 2-row sections) and with MIDI-sync rows shown: every visible child inside the overlay, no two children overlap, everything above the footer (height - pad - 20). Uses a fake `ProcessorBase` (`TestProcessorBase.h`); links ProcessorBase and the settings-page sources individually | `mu-core/Tests/SettingsOverlayFitTests.cpp` | Pass (24 configurations, 0 failures; deepest control 542 px vs footer 734 px at worst case; checked against a 300 px window, which fails as it should) |
| Algorithm name table invariants (sizes, ASCII names) | `mu-core/Tests/AlgorithmNameTableTests.cpp` | Pass (moved from mu-clid, Debug) |
| ModulationMatrix process semantics | `mu-core/Tests/ModulationMatrixTests.cpp` | Pass (moved from mu-clid, Debug) |
| UI ↔ APVTS scaling consistency | `mu-core/Tests/UIScalingConsistencyTests.cpp` | Pass (moved from mu-clid, Debug) |
| Insert DSP smoke (no NaN, sensible output range) | `mu-core/Tests/InsertDSPSmokeTests.cpp` | Pass (moved from mu-clid, Debug) |
| Filter DSP smoke | `mu-core/Tests/FilterDSPSmokeTests.cpp` | Pass (moved from mu-clid, Debug) |
| MIDI full-preset map | `mu-core/Tests/MidiFullPresetMapTests.cpp` | Pass (moved from mu-clid, Debug) |
| MIDI clock slave (shared mu-core): tempo estimate, block-start beat vs ground truth (steady + jitter), step-exact 16ths | `mu-core/Tests/MidiClockSyncTests.cpp` | Pass (moved from mu-clid, Debug) |
| Family transport rule (shared mu-core): host / no-position host / the three MIDI-clock Messages modes / own transport, Start from bar 1, mid-play mode switch | `mu-core/Tests/TransportResolverTests.cpp` | Pass (moved from mu-clid, Debug) |
| Send-FX DSP smoke | `mu-core/Tests/SendFXSmokeTests.cpp` | Pass (moved from mu-clid, Debug) |
| Host meter + bar start to bar.beat.sixteenth | `mu-core/Tests/BarPositionTests.cpp` | Pass (moved from mu-clid, Debug) |
| MIDI control map: learn, quantise, hand-off. The edit-while-messages-arrive race test is deterministic: a single-threaded phase proves all 8 actions perform, then a fixed 20000 rounds race a gated editor thread and assert no wrong action (no time window; a drain overlapping an edit drops its action by design, so no count is asserted). The save / load test uses a unique temp file so parallel runs cannot collide | `mu-core/Tests/MidiControlTests.cpp` | Pass (96 parallel runs under load, 0 failures, build 1271) |
| Hot-swap `Stager` (`HotSwapTests.cpp`): deterministic take-back versus flag (flagged, `consumeNow`, then `consume` applies exactly once, slot and full paths); threaded stress of 20000 rounds with a jitter between the token read and the compare-exchange, a gate that carries the payload id (apply asserts it, so a payload committed under another payload's boundary fails), a round that stages a newer payload after the boundary closed, and the applied count taken from the round pattern (8000) rather than from the test's own tally; a seeded-old-bug mode (flag re-reads the slot, ignoring the generation) must be caught. Mutation check: a production `flag` that ignores the generation fails the state-machine and stress tests. ThreadSanitizer and ARM runs stay with the build-PC item | `mu-core/Tests/HotSwapTests.cpp` | Pass (Debug) |
| Sidechain sources follow their layer through remove / swap | `mu-core/Tests/SidechainRemapTests.cpp` | Pass (moved from mu-clid, Debug) |
| MP3 samples open and decode | `mu-core/Tests/Mp3FormatTests.cpp` | Pass (moved from mu-clid, Debug) |
| `ProcessorBase::ScopedStorageRoot`: with a root set, settings and content sit under it and nothing is written to the real `TDP` folders; the default is restored (and nests) | `mu-core/Tests/ScopedStorageRootTests.cpp` | Pass (build 1306) |
| `ChannelState::reset()` equals the APVTS default of every `ch{N}_` parameter from the shared strip layout (`addChannelStripParams`), for all `kMaxLayers` strips, with default options and with mu-Clid's options; also fails if the strip gains a parameter the test does not know. mu-On's Bass / Rumble sidechain defaults are product options and are not covered (they differ from `reset()` by design; `resetLayerParams` follows with the real defaults) | `mu-core/Tests/ChannelStateResetTests.cpp` | Pass (2 tests, build 1271) |
| Stereo peaks (`MixerEngine::processBlock` driven through the render hook): master L / R follow the master pan (hard left leaves R silent, centred equal, combined = larger side), a mono output buffer reports its one channel as both sides, the reverb return L / R follow the return pan and an unused return reads zero on all three peaks. Mutation check not run | `mu-core/Tests/MixerStereoPeakTests.cpp` | Pass (3 tests; suite 283 results, 0 failures, Debug build 1409) |
| `LiveState` layer callbacks (`LiveStateTests.cpp`): `getLayerCount` / `getLayerColourIndex` come from the owner's callbacks and are read live; with none supplied one layer and the palette in order, wrapping at 8, negative index to 0 | `mu-core/Tests/LiveStateTests.cpp` | Pass (build 1410) |
| `ControlAttachment` seam: the factory slot is empty by default and is one object; a registered factory receives sink, view, `SurfaceLook` and folder and builds an attachment; status listeners are told on release and connect, also through a const reference, and not after removal | `mu-core/Tests/ControlAttachmentTests.cpp` | Pass (build 1410; 391 results, 0 failures) |

Note: the real product processors are built headless in each product's test target (the seam in [design-plugin-family.md](docs/design-plugin-family.md), "Headless product processors": `createEditor()` lives in `PluginEditor.cpp`, tests supply a one-line stand-in, `ProcessorBase::ScopedStorageRoot`, shared source list `mu-core/cmake/ProcessorTestSources.cmake`). Run one product's real-processor tests with `<exe> mu-<product>-processor`. Real-processor tests: mu-Tant `mu-tant/Source/Tests/ProcessorSeamTests.cpp` (construction, `resetLayer` equals a fresh voice across block sizes and insert algorithms, add / remove / swap / reset and `loadUserWavetable` against `processBlock` on a worker thread, `layerEditInFlight` skip), mu-Toni and mu-On `Source/Tests/ProcessorSeamTests.cpp` (layer ownership: `getLayer(i) == &getArp(i)`, `laneOf(voiceSlot(i)) == i`), mu-Clid `mu-clid/Source/Tests/ProcessorSeamTests.cpp` (rhythm edits and the loop-boundary swap commit against `processBlock`). Known gap logged by the mu-Tant test: `RingModInsert::reset()` leaves its frequency smoother. Known behaviour: a staged per-rhythm swap with "On master loop" timing and no master loop never commits. Status: Pass (build 1306).

---

## C++ unit tests — mu-control (device-independent controller layer)

Built as part of **ALL_BUILD** (no hardware, no product, no `ProcessorBase`; compiles the library sources directly). Build command:

```
cmake --build build --target mu-control-tests --config Debug && build/mu-control/Tests/mu-control-tests_artefacts/Debug/mu-control-tests.exe
```

Every surface case runs on three descriptions: Launchpad-shaped (8 x 8, side column, RGB), the mock grid (4 x 4, no side buttons, palette only, no blink, origin bottom-right) and a native-blink surface; the same model and mapping must serve all of them.

| Coverage | File | Status |
|---|---|---|
| Surface description: button counts and control list match the geometry; every listed control exists once, off-surface ids do not; `MockGridDriver` reports in surface coordinates despite its bottom-right origin, a closed driver reports and sends nothing | `mu-control/Tests/SurfaceDescriptionTests.cpp` | Pass (build 1410) |
| Surface mapping: default for both descriptions (top = items, side = layers bottom to top, grid row = layer), override replace / unbind / refuse off-surface, XML round trip of every action field, version and surface id, unknown action name dropped, other surface / newer or missing version / bad text / off-surface entries ignored, restore replaces earlier overrides | `mu-control/Tests/SurfaceMappingTests.cpp` | Pass (build 1410) |
| Surface model: top / side / grid presses perform the right `ControlAction`, release and value do nothing, rows past the layer count dark and unpressable, handle result is the sink answer, one light per control and all dark outside Performance mode, item and layer light states, dim = `dimScale` x bright, queued pulse (animated RGB, native, steady middle level) from an injected clock, animation timer only while an RGB light pulses, refused press changes no light, `LiveView` change re-renders, listener lifetime, a user override changes both press and light. Not yet covered (arrive with later stages): the flash phase and the clip-grid light states (M5), `FakeDevicePorts` and the driver tests (M2) | `mu-control/Tests/SurfaceModelTests.cpp` | Pass (build 1410) |

**Current status:** 27/27 results pass at build 1410 (Debug).

Boundary self-test: `pwsh tests/scripts/test-check-core-boundary.ps1` also covers the mu-control rules (a processor or shell header, a mu-core UI header, a plugin namespace, a `Devices/` file including the mapping or model, mu-core including mu-control, and clean controls) through `check-core-boundary.ps1 -ControlPath`. 8 cases pass.

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

## C++ unit tests — μ-Clid MIDI

Built on demand only (`EXCLUDE_FROM_ALL`, like mu-clid-tests):

```
cmake --build build --target mu-clid-midi-tests --config Debug && build/mu-clid-midi/mu-clid-midi-tests_artefacts/Debug/mu-clid-midi-tests.exe
```

The render tests drive `MidiRhythmCore` (the audio path `PluginProcessor` delegates to) from a bare processor, so they test the shipped code without the editor. `PluginProcessor` itself (transport derivation, the `processBlockBypassed` override, the rebuild timer) is still not linked; the bypass override is one call into the core and is not unit-tested directly.

| Coverage | File |
|---|---|
| Parameter layout: saved ids, old defaults, no voice / mixer / FX ids | `ParameterLayoutTests.cpp` |
| Accent knob velocities | `AccentVelocityTests.cpp` |
| Session state: own round trip, mu-Clid Lite session, older parameter-tree dump, unreadable state; restore onto a non-default instance resets what the state lacks (Lite session, empty Lite session, tree missing a key); unknown parameter nodes dropped; old rhythm name and colour handed back | `SessionStateTests.cpp` |
| Render matrix (44.1 kHz and 192 kHz, blocks 1 / 64 / 512 / 2048): plain playback, transport stop mid-note, MIDI note change mid-note, bypassed block, block skipped on lock contention (lock held from a second thread), `setStateInformation` mid-play. Asserts matched note-on / note-off pairs, none left active, no note longer than 20 ms plus one block | `MidiRenderTests.cpp` |
| Old state's rhythm name and colour land on the Rhythm after a restore | `MidiRenderTests.cpp` |
| Playback content (44.1 kHz, block 64, through the shipped core): E(4,8), E(8,8), E(0,8) on the 1/16 grid; hits / steps / rotation (0, 1, 3, -2) reach the shared `mu_core::HitGenerator` (played steps equal its pattern over two passes); a 5-step rhythm repeats on its own length; accent amount 0 / 50 / 100 gives the `accentVelocities` pair on the accented and plain hits; a stopped transport plays nothing and a restart from beat 0 sounds step 0; a start half way through a rest step plays the next hit half a step later; 90 and 140 BPM set the step gap. Mutation check: swapping accented and plain velocity in `MidiRhythmCore` fails 32 results | `MidiRenderTests.cpp` |
| Parameter change off the message thread: the listener allocates nothing (operator new counted on that thread against the host-notification baseline), the field is written at once, the pattern rebuild waits for the message thread, `mstrLoop` set | `MidiRenderTests.cpp` |

**Current status:** Pass, 78 results, 0 failures (Debug, build 1300). Mutation check: removing the lock-skip flush, or rebuilding the pattern on the calling thread, makes the render tests fail.

---

## C++ unit tests — mu-link

Console target `mu-link-tests`:

```
cmake --build build --target mu-link-tests --config Debug && build/mu-link/mu-link-tests_artefacts/Debug/mu-link-tests.exe
```

| Coverage | File |
|---|---|
| Server engine: client rings and mixing, `underrunFrames` per block and in the shared registry, left / right peaks per client and master (a stereo source gives different sides and the combined peak is the larger; a mono device reports one channel as both sides), output latency published in the transport, `requestClientDepth` writes the slot's `requestedDepthFrames` (negative clamps to 0, out-of-range slot ignored) | `mu-link/Source/Tests/ServerTests.cpp` |
| Client: the server can request a depth and the client follows it (a withdrawn request returns to the client's own depth), default depth, depth clamps (ring maximum, floor of one render chunk, negatives), underrun count and output latency read back, a depth sweep | `mu-link/Source/Tests/ClientTests.cpp` |
| Window: Latency Low / Normal / High asks every connected slot for 2048 / 0 (the app's own default) / 8192 and is remembered with the settings; the Dropouts lamp goes red on an underrun, latches amber, and clears on a click or a Latency change | `mu-link/Source/Tests/MuLinkWindowTests.cpp` |

**Current status:** Pass, 81 results, 0 failures (Debug, build 1409).

**Latency coverage and gaps:** covered are the three choices, the protocol-v4 request field, the underrun counter, the client following the server depth, depth clamps and the lamp. Not covered: the MIDI due-time compensation and listening re-validation of each Latency setting on real devices (hardware items), and the literal value 4096 (the test compares the client default with `kDefaultTargetDepthFrames`, which is defined as `kDefaultClientDepthFrames`, so changing the constant would not fail a test).

---

## CLAP validation (clap-validator)

Tool: clap-validator 0.4.1, currently at `C:\Users\andyt\AppData\Local\Temp\claude\d--Dev-mu\622d17e1-0a58-4a44-b939-6ac3ddfcacac\scratchpad\clap-validator\clap-validator.exe` (a session scratchpad copy; fetch it again if that folder is gone).

**Debug builds:** the validator's process tests activate a plugin twice, which trips an assertion dialog in the clap-helpers wrapper in Debug (`plugin.hxx:375`, `!_isActive`). Against a Debug `.clap` run only the filters `state-`, `param-`, `features` and `audio-ports` (`-t <filter>`); run the full suite only against Release builds.

| Product | Result |
|---|---|
| All products, `state-*` | 4 pass, 0 fail |
| All products, `param-conversions` | Pass |
| mu-Tant, mu-Clid, `param-default-values` | Fails by design: the user's saved `_default` preset is loaded at startup, so values differ from the declared defaults. Not a defect |
| mu-On, full suite | 33 pass, 0 fail |
| mu-Tant, mu-Clid, full suite | 32 pass, 1 fail (the case above) |

Recorded as reported by the main conversation; not re-run by the test steward.

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
