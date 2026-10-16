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
| `mu_pp::PerformanceFile` (`PerformanceFileTests.cpp`, 7 tests): round trip of name, description, items, product and cached names (absent idx = empty item); relative path inside the preset folder, absolute outside, both resolve back; status Ok / Missing / OtherProduct / Empty (out of range = Empty), missing and foreign items stay listed and survive save and load; more than 8 items and bad or repeated idx counted not loaded (first wins); empty list, missing file, junk XML, wrong root, newer format refused with the target untouched; out-of-range `setItem` refused and `clearItem`; save over an existing file replaces it whole with no temp file left, and an uncreatable path returns false. **Pass (Debug, scratch Ninja tree, mu-core 423 results, 0 failures; the 7th test is new and was run)** | `mu-core/Tests/PerformanceFileTests.cpp` |
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
| Clip bank on the real processor (`ClipLaunchTests.cpp`, 12 tests): store, change, stopped launch restores the rhythm with bank, name and colour untouched; a clip holds the sample path and no audio and a launch reloads the sample; a clip from a `.muRhythm` launches as that file loads; every non-clip load path clears `activeClip`, a launch keeps it; bad tag, unreadable file, bad index, empty clip and a staged full preset refused; launch while playing is pending and commits at the boundary (`onLayerPresetCommitted`); live pad launch through mu-Clid's stager (stopped = at once, playing = queue and land, a load or full preset settles it); a launch whose stage hook staged nothing is refused; a cancelled launch never commits and a layer-preset load supersedes a launch; banks follow their rhythm through swap and remove, a new or reset rhythm starts empty; `<Clips>` and `activeClip` round trip through a full preset and a host session; a staged full preset carries its banks (a smaller one empties dropped rhythms); a file without `<Clips>`, a legacy session and mu-Clid MIDI-shaped files load clip-free. **Pass (Debug, e4c58cf run: mu-Clid 199 results, 0 failures)** | `ClipLaunchTests.cpp` |
| FX tail across a full-preset commit (`FxTailTests.cpp`, offline, real processor, shared `mu-core/Tests/FxTailChecks.h`, block sizes 64 and 512): a performance switch is staged mid-bar and commits on the bar line (not before), current item is B; against a control run with no switch, the switched tail over the second after the commit matches to better than -60 dB, has no run of 16 or more exact-zero frames and no click above 3x the control's largest step; the control tail must itself be audible. **Pass (Debug, e4c58cf run: mu-Clid 199, 0 failures)** | `FxTailTests.cpp`, `mu-core/Tests/FxTailChecks.h` |

**Current status:** 140/140 tests pass at v1.0.708.  Layer-structure suite re-run: 179 results, 0 failures (Debug, build 1409), including the tied-mid-release and stereo-peak tests.

**Result counts at e4c58cf (Debug, last runs, all 0 failures):** mu-core 457, mu-Clid 199, mu-Tant 180, mu-Toni 42, mu-On 61, mu-Clid MIDI 78, mu-link 83, mu-control 85. mu-Clid MIDI has no clip bank by design (no strip), so no clip tests.

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
| Mixer block values (`MixerEngine::processBlock`, bit-exact): level, pan law, trim, master, direct buses, sends and returns, mute/solo truth table, sidechain. **Changed for the disabled-slot rule:** the rig now disables the FX slots (a disabled slot returns silence) and gets exact returns from 1 ms zero-feedback Effect(Echo)/Delay slots fed a constant. **New:** a disabled Effect (Chorus and Echo algorithm), Echo delay, Delay, Reverb, and all three each leave the main output equal to the dry-only render bit for bit with silent returns; a disabled Effect adds nothing through Effect->Delay / Effect->Reverb routing; Echo repeats reach the reverb through Effect->Reverb after the channel sends go silent; reverb return fader/pan scale the wet exactly against its own unity render. **Changed for the level-following sidechain duck** (full duck at a post-trim source level of 0.25, proportional below; release floored at 30 ms): the order tests (mutual ducking, higher / lower index, block-size determinism) are re-derived with power-of-two levels; the "below threshold" test is now a proportional-duck sweep (pre-trim 0.125 / 0.25 / 0.5 / 1.0, every block size, bit-exact) plus a non-dyadic 0.05 case (tolerance) and a silent-source case. **New:** a decaying 55 Hz kick (peak 0.8, 180 ms, every 0.5 s, 5 ms attack, 100 ms release, amount 1) must let the follower fall below 0.3 before each hit in beats 4 to 12 (the old gate held about 0.998) and still duck above 0.5; a release below 30 ms equals exactly 30 ms and 60 ms is honoured (follower after 1440 silent samples). Expectations derived by reasoning, not measured; the kick thresholds come from a throwaway simulation (min 0.135, mean 0.52) | `mu-core/Tests/MixerBlockValueTests.cpp` | **Not run (laptop, no toolchain)**; previous result unknown for the changed tests, re-run on the build PC |
| `LiveState` layer callbacks (`LiveStateTests.cpp`): `getLayerCount` / `getLayerColourIndex` come from the owner's callbacks and are read live; with none supplied one layer and the palette in order, wrapping at 8, negative index to 0 | `mu-core/Tests/LiveStateTests.cpp` | Pass (build 1410) |
| `ControlAttachment` seam: the factory slot is empty by default and is one object; a registered factory receives sink, view, `SurfaceLook` and folder and builds an attachment; status listeners are told on release and connect, also through a const reference, and not after removal, and are told when the attachment is destroyed (a watcher never keeps a stale pointer); the fake implements the new `closeNow` | `mu-core/Tests/ControlAttachmentTests.cpp` | Pass (build 1410; mu-core 408 results, 0 failures) |
| Controller shell (`ControllerShellTests.cpp`, a fake `ControlAttachment` and `TestShellBackend::controller`, no mu-control): `ControlHost` builds nothing with an empty factory slot, nothing without Performance mode support, a sink or a view, hands the factory the backend's sink, view, look and folder and publishes the pointer, and shuts down in the order close-now, processor forgets, destroy (repeat calls harmless); `QuitFlow` Close / Save / Cancel give the devices back before quitting or saving; `makeSurfaceLook` reads the theme's layer, fault, accent, dim and pulse values; the Settings "Controller" group is absent without an attachment, appears and goes with it, button reads Release or Connect per status and calls through, lamp colours per status, and still fits the page at both sizes and scales; the transport bar status line announces Connected and Released but not adoption or light changes, keeps Lost until the pad returns, and clears it when the attachment is destroyed or Performance mode ends. **Gap:** the `CloseConfirmWindow` wiring in the shell is covered by code review only (`QuitFlow` is tested, the window is not); the shell's "not in `--render`" rule is in `StandaloneShell.h` and untested | `mu-core/Tests/ControllerShellTests.cpp`, `ControlAttachmentTests.cpp` (listeners told on destruction), `TestShellBackend.h` | Pass (16 tests; mu-core 408 results, 0 failures, Debug build 1410) |
| Clip bank data (`ClipBankTests.cpp`, 11 tests): new bank empty at the family limit of 8; store, name, clear, clearAll; out-of-range refused; an invalid tree empties the clip; deep copy on store, get and bank copy (no aliasing); `writeTo` / `readFrom` round trip with names and the empty-name case; `writeTo` replaces an old child and writes nothing for an empty bank; `readFrom` tolerates missing, invalid and malformed input and deep-copies out of the source; a Layer's bank survives assignment, copy and `adoptDefaults` | `mu-core/Tests/ClipBankTests.cpp` | Pass (e4c58cf run, mu-core 457, 0 failures) |
| Clip store and launch on `ProcessorBase` (`ClipLaunchTests.cpp`, test backend with a stored-tree layer preset, 11 tests): store then stopped launch restores the layer, leaves strip and identity alone, never writes the bank; empty clip, bad index and staged full preset refused; launch while playing is pending until the boundary and fires `onLayerPresetCommitted`; a launch equals loading the same layer preset file; `loadClipFromFile` wrong tag and unreadable file raise `onLoadError`; a non-clip load clears `activeClip`, a launch does not; `clearClip` empties, drops active and the mask follows; the screen StoreClip / ClearClip / LaunchClip actions (launch works with the mode off); storing over a queued clip drops that launch, Performance mode refuses the writing actions; a store is authoritative over a staged swap and refused while a full preset is staged; a clip survives the layer's own edits and is independent of the live layer | `mu-core/Tests/ClipLaunchTests.cpp` | Pass (e4c58cf run, mu-core 457, 0 failures) |
| Clip strip UI (`ClipStripTests.cpp`, 14 tests): lamp state and level per row of the table (playing, available, empty, queued pulsing 2 Hz, muted playing flashing 1 Hz); follows the live view and a new playing clip flashes once; click launches, playing pad = mute, queue behind it = cancel; click on an empty pad stores, Shift+click stores anywhere asking only to replace a stored non-playing clip; clear asks first and works only on a stored clip, right-click menu maps to store and clear; bad pad, no sink or no view, and a confirm answered after the layer changed write nothing; Performance mode makes the strip read-only but live, the shield stops the pointer reaching it; strip width 243 / 251 and the Large scale; header bar hides the strip unless asked and fits between dropdown and Save at both sizes; Reset confirm adds the clips line only for a non-empty bank; painting lights a playing pad and not an empty one. **Gap:** each product editor turning the strip on is one line in `LayerPanel` (`setShowClips` when the processor has a capture hook), covered by eye only | `mu-core/Tests/ClipStripTests.cpp` | Pass (e4c58cf run, mu-core 457, 0 failures; painting not pixel-asserted) |
| Clip live state (`LiveStateTests.cpp`, about 17 clip tests with fake owner callbacks): launch while playing queues and the commit makes it playing and records it; stopped applies at once; empty pad, bad index, missing layer and refused launch change nothing, mode off allows only a launch; playing pad mutes and unmutes, with another queued the first press cancels the queue; a layer-preset stage or cancel supersedes a queued clip; a non-clip stage, added or reset layer forget that layer's clips only; a full preset forgets every clip on commit, not while only staged, and one cancelled from outside leaves them; a landing full preset or restored session shows the saved playing clips; a mute changed outside the pad refreshes lamps; a performer-cancelled item does not read as a landing; `remapLayers` carries clips and masks and drops a moved layer's queue; clearing a clip stops it or cancels its queued launch; ClipNext / ClipPrev step stored clips, wrap, never mute, refuse with no other clip; a restored session forgets clip state; no owner callbacks = refused with safe getters. Shared checks (`mu-core/Tests/ClipLaunchChecks.h`, run on the real mu-Tant, mu-Toni and mu-On processors): stopped launch plays at once, playing queues and lands, lamps follow; supersede and cancel; remove and swap carry the playing clip and mask; **reset layer plays no clip and clears the mask**; stored clip equals file load; rejects; boundary landing. mu-On adds that a clip carries its engine. Persistence: `LayerOwnershipTests.cpp` writes `<Clips>` per layer (none for an empty bank), loads a state without `<Clips>` empty, ignores extra children and empties the rest on a smaller state. Not covered: a block-size sweep around the clip boundary commit | `mu-core/Tests/LiveStateTests.cpp`, `ClipLaunchChecks.h`, `LayerOwnershipTests.cpp`, `mu-tant` / `mu-toni` / `mu-on` `Source/Tests/ClipLaunchTests.cpp` | Pass (e4c58cf run: mu-core 457, mu-Tant 180, mu-Toni 42, mu-On 61, 0 failures) |
| FX tail across a full-preset commit on the real mu-Toni processor (same shared `FxTailChecks.h` as mu-Clid, block sizes 64 and 512; a fresh layer arps at its defaults) | `mu-toni/Source/Tests/FxTailTests.cpp` | Pass (e4c58cf run, mu-Toni 42, 0 failures) |

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
| Surface model: top / side / grid presses perform the right `ControlAction`, release and value do nothing, rows past the layer count dark and unpressable, handle result is the sink answer, one light per control and all dark outside Performance mode, item and layer light states, dim = `dimScale` x bright, queued pulse (animated RGB, native, steady middle level) from an injected clock, animation timer only while an RGB light pulses, refused press changes no light, `LiveView` change re-renders, listener lifetime, a user override changes both press and light. Clip grid light states: dark while no clip is stored or the row has no layer (even with stale state), stored dim, playing bright, queued pulses between dim and bright on RGB and is steady on the native and mock surfaces, a muted layer's playing clip flashes between bright and dim and is never dark, a clip that has just started plays one brief burst then settles, entering the mode on an already playing clip does not burst | `mu-control/Tests/SurfaceModelTests.cpp` | Pass (build 1410) |
| Launchpad X protocol: recorded setup bytes (Programmer / Live, aftertouch off, layout read, inquiry), inquiry reply recognition (maker, family, short, any channel), `10 * row + col` numbering and CC buttons, every pad and button round trip, velocity 0 / note-off / CC 0 release, ignored input (logo, aftertouch, SysEx incl. unterminated, strays, truncated), any MIDI channel, several messages in one buffer, RGB lighting bytes and 7-bit scaling, 81-entry batch split, palette static / flash / pulse forms, nearest palette entry | `mu-control/Tests/LaunchpadXProtocolTests.cpp` | Pass (build 1410) |
| Launchpad X driver over `FakeDevicePorts`: description, open sequence and port patterns, failed open sends nothing, input to `SurfaceEvent` with y from the top, one batched lighting SysEx, only changes sent, RGB ignores blink mode, palette driver forms, close (pads off, Live, ports released), destructor, unplug / replug refresh with the latest frame, close while unplugged | `mu-control/Tests/LaunchpadXDriverTests.cpp` | Pass (build 1410) |
| Controller manager over `FakeDevicePorts` / `FakeLiveView`: detection by name with nothing opened, NotFound to Found on appearance, open only when the mode turns on and Live back when it turns off, built-with-mode-on, device listed but unopenable retried, release / connect (mode on and off, no device), unplug = Lost (sticky across scan ticks) and replug repeats setup, leaving the mode while Lost, MIDI-thread press delivered on the message thread and dropped when queued at mode end or after destruction, blink timer only while a pad pulses, delta lighting, quit hook (once, repeat calls and the seam call change nothing, also with the mode off; an explicit Connect undoes it and the pad is given back again on destruction), end to end with a real `LiveState` (top 3 queues item 2, the same press again leaves it queued, pressing the playing item cancels a queued switch, a side press selects the layer and the lights are rewritten; Launchpad driver over fake ports), Live exactly once on destroy (also after unplug / replug) and none while Lost, connect and release while Lost, listeners notified once per status change | `mu-control/Tests/ControllerManagerTests.cpp` | Pass (build 1410) |
| Launchpad X colour correction (`LaunchpadXColourMapTests.cpp`, 5 tests): each of the 12 known screen colours maps to its tuned pad colour and the table rows match those 12; dim is applied after the correction, in pad space; an unknown colour falls back to a sane correction that is monotone in brightness; the palette form is not colour-corrected. `LaunchpadXDriverTests.cpp` was edited to expect the corrected bytes (`padRgbFor`) in the batch and single-pad lighting messages. Files exist in the working tree, uncommitted, added after e4c58cf | `mu-control/Tests/LaunchpadXColourMapTests.cpp`, `LaunchpadXDriverTests.cpp` | **Unrun** (expect 90 results; `cmake --build build --target mu-control-tests --config Debug` then the exe) |

**Current status:** 85/85 results pass at e4c58cf (Debug; includes the clip-grid light states). The 5 colour-map tests and the edited driver expectations are unrun (expect 90). Not covered: a driver swap and a user port override (neither exists in the manager yet), and `JuceDevicePorts` itself (real MIDI ports; hardware only, below).

**Hardware checks (Launchpad X, build PC; the fake-port tests cannot see these):** run by Andy with the M3 probe harness; findings belong in design-launchpad.md §3.5.

| Check | Status |
|---|---|
| Probe step 1: Programmer mode on, RGB batch lighting (`...0C 03 <03 idx r g b>...`) of the pads | Verified on the real pad (Andy, M3) |
| Probe step 3: pad / button input decode (note and CC numbering, velocity) | Verified on the real pad |
| Probe step 4: batch lighting of all pads in one message | Verified on the real pad |
| Probe step 5: `ControllerManager` demo (open on mode, lights follow, Live restored on release) | Verified on the real pad |
| Unplug and replug while in use: Lost, then refresh of setup and frame | **Not verified** (fake-port tests only; the real `JuceDevicePorts` raises no connection event until the hot-plug watcher sees it, so this needs a real run) |
| Logo press (CC 99) value and whether it needs Programmer mode | **Not verified** (driver ignores it; behaviour unrecorded) |
| Live mode restored on quit, window close and OS-requested quit | **Not verified** (the shell hook is built; run with the real shell in M4) |
| M4 pad test with the real shell: top row switches items, side column selects layers, the screen follows, Live restored on exit | **Not yet done** (needs Andy on the build PC with the pad) |
| Palette fallback forms (static / flash / pulse) and `LaunchpadXPalette` RGB values on the pad | **Not verified** (RGB path only was tried) |
| Pads and buttons dark after `close` on the real pad | **Not verified** as a separate step (seen implicitly in the demo only if Andy watched; confirm) |
| Two processes sharing the ports | **Not verified** |

Boundary self-test: `pwsh tests/scripts/test-check-core-boundary.ps1` also covers the mu-control rules (a processor or shell header, a mu-core UI header, a plugin namespace, a `Devices/` file including the mapping or model, a `Surface/` or `Manager/` file including a `Devices/` header, mu-core including mu-control, and clean controls) through `check-core-boundary.ps1 -ControlPath`. 10 cases pass (whole fixture set).

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

## Layer extraction safety net

**Status: planned. Nothing in this section is written or run yet** (plan by the test steward, 2026-10-15, for the detachable-layer extraction of [docs/mu-tronic/design.md](docs/mu-tronic/design.md) stages T3 to T7; the stage order and file moves are in `docs/mu-tronic/extraction-plan.md`, owned by the architecture-steward). Andy's ruling: the extraction comes first and includes the live mu-Clid and mu-Tant. So the net must prove "no audible, saved or real-time change" for four products, two of them shipping. Counts below are from the last recorded runs (e4c58cf, Debug) and were not re-run for this plan.

### 1. What already protects each product, and what it does not

| Product | Protects today (suite size) | Does not protect (the extraction can break these unseen) |
|---|---|---|
| **mu-Clid** (`mu-clid-tests`, 199 results, ~219 test cases) | Parameter and preset round trips (`RhythmParamRoundTripTests`, `PresetXMLRoundTripTests`, `PresetMigrationTests`, `KindedPropertyRoundTripTests`), layout (`ApvtsLayoutTests`), sequencer timing (`RhythmSequencer*Tests`, `SwapCommitTimingTests`), hot swap and clips on the real processor (`HotSwapCancelTests`, `ProcessorSeamTests`, `ClipLaunchTests`), tails (`FxTailTests`, `ClidLayerStructureTests`), MIDI-out mode (`MidiOutputModeTests`), scripted edits (`ClidScriptedEditRenderTests`, `ClidEditContinuityTests`). Listening: `CLID_roundtrip`, `TS_swap`, `T11` to `T13`, `A1` to `A9`, `CLID_midi_clock`. | No test pins the **rendered sample values** of a full mixed block (engine, insert, sends, master) for any preset; the tests assert shapes (RMS windows, onset counts, centroid), so a gain-staging or summation-order change of a fraction of a dB passes. The per-block modulation glue (`PluginProcessor_Modulation.cpp`) is only seen through A3 to A7 thresholds. Retire-tail rendering is checked in one scenario. Sample loading at stage time (the `preloadStaged` move) has `EmbeddedSampleTests` only. No parameter-id manifest across the whole layout. |
| **mu-Tant** (`mu-tant-tests`, 180 results, ~182 cases) | Gate shape and rules (`GatePatternTests`, `GateStageTests`), voice DSP smoke (`SynthVoiceTests`, `SynthDSPTests`), modulators, layout, persistence (`MuTantPersistTests`, `PresetRoundTripTests`), hot-swap boundary (`HotSwapBoundaryTests`), real-processor seam (`ProcessorSeamTests`: reset equals fresh, edits against `processBlock` on a worker thread, wavetable load), `ClipLaunchTests`. Listening: `TANT_preset`, `TANT_roundtrip`, `TANT_midi_clock`, `mutant_drone`. | **Note mode** (held-note stack, gate ramp on the final mix) has no direct test and design.md section 3.5 moves it per layer: the highest-risk behaviour change in the plan. The render chain that moves (`renderVoice`, filter and pitch envelopes, retire fade, dual filter, X-Mod) is covered by smoke tests (`SynthVoiceTests` asserts "makes sound / no NaN") not by values. Wavetable bank sharing (`bankLock`) has no concurrency test with two bank users. |
| **mu-Toni** (`mu-toni-tests`, 42 results, ~57 cases) | `ArpRunnerTests`, `ArpSequencerTests`, `StepPatternTests`, `ToniVoiceTests`, `ModArcTests`, `PrefixMigrationTests`, `LayerCountPersistTests`, `ClipLaunchTests`, `FxTailTests`, seam. Listening: `TONI_roundtrip` (legacy dump to sample-identical re-render), `TONI_swap`, `TONI_slot_swap`, `TONI_host_lock`, `TONI_midi_clock`. | Smallest suite of the four, for the product that goes first (T4). `readVoice` (modulation resolve into `ToniVoiceParams`) and the global held-note stack / root-by-MIDI / MIDI-trigger mode that become per layer have no test. The voice `juce::Random` is default-constructed, so noise renders are not repeatable (section 2). |
| **mu-On** (`mu-on-tests`, 61 results, ~65 cases) | `SequencerTests` (beat to step, swing, accent), `EngineTests` (Kick and Hat sound and decay only), `GrooveVoicesTests`, `ModulationTests` / `ModArcTests`, `EngineMigrationTests`, `PrefixMigrationTests`, `SlotStateTests`, `PresetTests`, `ClipLaunchTests`, seam. Listening: `ON_roundtrip`, `ON_swap`, `ON_midi_clock`. | Bass, Snare, Rumble engines have no sound test at all; no engine has a value-level test. The global swing and accent becoming per layer (T6) and the single shared grid becoming a per-track row are pinned by nothing. Rumble reading another layer's audio, and bass to kick sidechain, are untested. |
| **mu-core** (`mu-core-tests`, 457 results, ~520 cases) | The contract pieces that stay: `LayerOwnershipTests`, `LayerAccessTests`, `HotSwapTests`, `SwapTimingTests`, `ClipBankTests`, `ClipLaunchTests`, `ChannelStateResetTests`, `SidechainRemapTests`, `MixerStereoPeakTests`, `TransportResolverTests`, `FxTailChecks.h`, `ProcessorSeamHelpers.h`. | No test renders a **block of mixed audio and checks values** through `MixerEngine` (peaks only). Nothing tests `Layer::render` yet (it does not exist). |
| **mu-Clid MIDI** (`mu-clid-midi-tests`, 78 results) | Render matrix over `MidiRhythmCore` (the shared core `Rhythm`), session state. | Protects only the shared `mu_core::Rhythm` / `HitGenerator` / `RhythmSequencer`. It is the free canary: it must stay green and unchanged through T7. |
| **Cross-product** | `ApvtsLayoutTests` per product; `ProcessorSeamTests` per product; the four `*_roundtrip` listening tests; pluginval (Windows); clap-validator. | **No** frozen manifest of parameter ids; **no** corpus of pre-refactor sessions re-saved and diffed; **no** automated real-time-safety check of `processBlock` beyond the seam tests' worker-thread runs; pluginval and the listening suite are run by hand, per release, not per stage. |

Listening-suite facts that matter here: the four `*_roundtrip` tests already render twice (from the saved preset and from the saved host session) and require sample identity, which catches save/load asymmetry but **not** a refactor that changes both paths identically. That is the gap the golden harness fills.

### 2. Golden-render harness (build FIRST, before any code moves)

**What it is.** One shared header, `mu-core/Tests/GoldenRender.h` (reused by every product, per the family rule), plus one `GoldenRenderTests.cpp` per product test target. It builds the real headless processor (the `ProcessorSeamTests` pattern: `ProcessorBase::ScopedStorageRoot`, stand-in editor, `mu-core/cmake/ProcessorTestSources.cmake`), loads a scenario's state, drives `prepareToPlay` and `processBlock` directly, and fingerprints the output. In-process, not the `--render` CLI: it is faster (no process start), can capture MIDI out and per-channel peaks, and can use a ragged block schedule. The CLI path stays covered by the existing listening tests.

**A scenario is** (all in `tests/golden/<product>/<name>/`): `state.xml` (a full preset tree or a host session, frozen; hand-built from `tests/presets` or a factory preset), `scenario.json` (sample rate 48000; block schedule; length; tempo and host start beat; scripted events: MIDI notes and CC with sample offsets, parameter sets, a staged swap or clip launch at a block index; which layers are soloed for stems), and `golden.json` (the reference). Scripted events go in at block indices, never at wall-clock times.

**What is recorded (the reference).** For each of: master L/R, then each active layer's stem (soloed with all sends 0, master insert off, so the stem is that layer through its own insert and strip), and Clid MIDI-out events:
- a 64-bit hash (FNV-1a over the raw IEEE-754 bit patterns of each channel) per fixed segment of 256 frames, plus the segment RMS in dBFS, so a mismatch names the first diverging segment and how loud;
- a whole-render hash;
- the end-of-render values of mixer peaks, per-channel and master, and `insertGainReduction` of each layer;
- a hash of `getStateInformation` after the render (links to section 4).
Nothing stores raw audio in the repo. On a mismatch the harness writes the actual render and the reference-build render (if available) as 24-bit WAVs to `tests/_out/golden/` for diffing.

**Equality rule.**
- **Same machine class, same config, same compiler (Windows x64, MSVC, Debug): bit-identical, tolerance 0.** A pure move of code must not change one bit; extraction stages move code and do not change arithmetic. This is the gate.
- Different config or platform (Release vs Debug, Linux): goldens are recorded per `windows-x64-debug`, `windows-x64-release`, `linux-x64-release` (the CI config) in separate files (`golden.<config>.json`). Never compare Debug against Release, because MSVC float behaviour differs between them. Release and Linux goldens are recorded when first needed (the release gate) from the same baseline commit.
- A per-scenario `tolerance_abs` field exists, **default 0**, and may only be set with a written reason in `scenario.json`. When set, the check is max per-sample absolute difference at or below the value (stored as 24-bit LSBs, like `run-listening-tests.py`), and the hash column is ignored. Intended use: none today. Not allowed: setting it to make a failing stage pass. If a stage legitimately reorders float arithmetic (summing layers in a different order, applying a gain before rather than after a sum), the right answer is to keep the old order; if that is impossible, the stage records a deliberate golden update (below) with the measured difference in dB, signed off by the dsp-controller.
- **Known behaviour change that cannot be a tolerance:** moving mu-Tant Note mode from the final mix to a per-layer ramp (design.md 3.5) changes audio whenever an FX tail or a second layer is present (the old gate also gated the FX return). The Note-mode golden scenarios therefore split in two: the layer stem (must stay identical if the per-layer ramp matches the old ramp maths) and the final mix with FX (expected to change; a decision for Andy and the dsp-controller before T5, recorded as a deliberate update with an audible check). Pin the old behaviour in a new test first.

**Randomness.**
- Fixed-seed already: mu-On `SampleChannel` (0x4a7 / 0x5b2), `BitcrusherInsert` dither, `VocoderInsert` noise. Deterministic by construction.
- **Not repeatable today (finding): mu-Tant `NoiseGen` (default-constructed `juce::Random`, seeded from the system clock) and mu-Toni `ToniVoice::rng`.** A scenario with the noise source on cannot be bit-identical across runs. Until the product code seeds them (a source change to request from the main conversation: construct with a fixed seed on `reset()`, which also makes retire/prepare repeatable), golden scenarios keep the noise level at 0, and the noise path gets a separate statistical test (RMS and spectral tilt within bounds, not a hash). Seeding them is itself a deliberate one-time golden update (it only affects noise-on scenarios, which are added after the seed fix).
- mu-Tant gate probability and any other random playback rule: the harness must run with the rule's seed fixed or probability 0/1. Open check: find the generator for gate probability (the persistence file mentions it; the playback code was not located in this review) and list it in the scenario notes.
- A harness self-test renders each scenario twice in one process and twice in two processes and requires identical hashes. A scenario that fails its own repeat test is not allowed into the set (this is how a hidden clock-seeded generator is found).

**Wavetable bank, samples, time.**
- Wavetable bank: scenarios use the factory (built-in) tables only, plus one small user wavetable checked into `tests/golden/fixtures/` and loaded by relative path under `ScopedStorageRoot`. The golden records a hash of the bank contents it rendered with, so a bank change is reported as a bank change, not an engine change. The user's content folder (`TDP/muTant/Wavetables`) is never read (not repeatable across machines).
- Samples (mu-Clid, mu-On sample channels): fixtures only: the embedded samples and one or two short WAVs in `tests/golden/fixtures/`, referenced by relative path. No path under `TDP/muClid/Samples`.
- Time: no wall clock anywhere. The transport is the explicit host position (the `--host-start-beat` mechanism, in-process), fixed tempo 120 and a second scenario at 93.7 BPM (non-integer samples per beat); sample rate 48000 and one 44100 scenario per product. Anything that polls a timer (UI timers, the rebuild timer) is not in a headless render; the harness asserts `Time::getMillisecondCounter` is not read on the audio path by the section 6 scan.
- Block schedule: scenarios run twice, 512 fixed, and a ragged schedule (1, 7, 63, 64, 129, 512, 1024, 3 repeated) that includes 1-sample blocks and a block larger than the prepared size. Engines are not required to be block-size invariant, so each schedule has its own golden; the check is "same schedule, same hash".

**Scenario set (about 10 per product, 1 to 4 seconds each; chosen so every moving piece is exercised).**
- mu-Clid: factory state; `A3_filter` (modulation); `A8_tail` (insert tail) with stop mid-tail; a sample rhythm with the fixture WAV; an 8-rhythm full preset (all layers) with the sidechain and a send FX return; MIDI-out mode (events hashed); a full-preset hot swap (`TS1` to `TS2`) staged at block N under Bar timing; a layer swap and a clip launch; reset/remove/swap of a layer during play (hash of the following output); one 44.1 kHz. A `.muRhythm` from the content folder is copied into fixtures, never read in place.
- mu-Tant: `To and Fro` and `Acid Cellar`; Note mode on with scripted held notes (stem and mix, see above); dual filter series and parallel; X-Mod; user wavetable; ring tap and insert; gate with a held tail across stop; hot swap; wavetable load during play (the bank lock).
- mu-Toni: factory; `TONI_legacy`; MIDI-trigger mode with held notes and root-by-MIDI; modulation matrix; swap; 4 layers with different scales.
- mu-On: factory groove; `ON_legacy` (the five-lane shape); each engine isolated (Kick, Bass, Hat, Snare, Rumble) so T6's per-engine equivalence has a reference; swing and accent non-default; bass to kick sidechain; Rumble reading a source layer; engine change mid-play.
- mu-Clid MIDI: unchanged canary; its existing 78 results are the gate, plus one golden of the emitted MIDI for a fixed rhythm (cheap, protects the shared `Rhythm`).

**Where goldens live, size, and how they change.**
- `tests/golden/<product>/<scenario>/` in git. Each `golden.json` is about 750 segments x 2 channels x about 40 bytes, 30 to 100 KB for a 4 s scenario with stems; 40 scenarios total is a few MB. `state.xml` is the frozen input (a preset tree, 5 to 200 KB). Total under 10 MB. Large audio is not stored.
- A golden is recorded **once, from the pre-refactor baseline commit** (the commit on which this section is signed off), by `pwsh tests/scripts/update-golden.ps1 -Product <p> -Scenario <s> -Reason "<text>"` (to be written). The script refuses to run on a dirty tree, refuses without a reason, writes the reason, baseline commit, build number, config and compiler string into the file, and prints the segment-by-segment diff against the old reference. There is no "update all" switch.
- A deliberate update is a separate commit that only touches goldens, with a message saying what behaviour changed and why it is correct; the dsp-controller signs the reason before the commit (CLAUDE.md: never edit an expectation just to turn a run green). A stage may not change a golden in the same commit as the code move.
- The harness refuses to run if the golden's recorded `schema` or scenario hash (state file + scenario.json) does not match the files, so a hand-edited state cannot silently pass.

### 3. Layer-vs-processor equivalence test (works while both paths exist)

Goal: the same state rendered through the old per-product processor path and through `Layer::render` (via the shared `renderLayers`) must match sample for sample. This is stronger than the golden because it is immune to compiler and config differences and needs no stored data.

Design:
1. **Two switchable paths in one binary during each stage.** Each stage that moves a product's render chain keeps the old chain callable beside the new one behind a processor-level switch (`useLayerRender`, test-controlled, default off until the stage gate), then deletes the old chain after the stage passes. This is a requirement on the coder for T4 to T7 (tell the architecture-steward so it is written into extraction-plan.md).
2. **Harness.** `LayerEquivalence.h` in `mu-core/Tests/`. Build two identical processors A and B from the same scenario (section 2 files), set B's switch on, drive both with the same block schedule and the same scripted events, and compare per block: the master output, and each channel's pre-mixer output. For the pre-mixer comparison the harness needs a read-only tap on what each channel hands the mixer. If no such tap exists, ask the main conversation to approve one test-only accessor on `ProcessorBase` (per the test-steward rule: no test-only hooks in production headers without approval); without it, the per-channel check uses soloed stems (master with sends off), which is exact only if pan and fader are unity and the pan law is applied identically in both (it is, since the mixer is shared).
3. **Equality:** bit-identical on the same binary (tolerance 0, no exceptions: both paths run the same compiled maths). Compare including the silent tail after stop, 1-sample blocks, and the first block after a swap.
4. **Coverage:** every scenario of section 2, per product, per engine type for mu-On (Kick, Bass, Hat, Snare, Rumble), each at fixed and ragged schedules. Plus the cases the processor path hides: a layer rendered with an empty slot beside it; priority ordering (Rumble after its source); lock miss (the layer skipped silently, transport still advancing, state identical afterwards); a type change that parks a layer in the retire bin and checks the old layer's fade then the freed-off-thread flag.
5. **After the stage:** the equivalence test is kept as the mu-Tronic test of design.md section 7: a mu-Tronic slot of type X, same parameters and MIDI, equals the product's own layer (built from the same code, so it passes by construction and fails the day the two diverge).

### 4. Save/load compatibility corpus (collect now)

Collect before any code moves, from the baseline commit, into `tests/golden/compat/<product>/` with a `MANIFEST.txt` of SHA-256 hashes (frozen; an edit to a file needs a reason in the same way as a golden).

| Source | Take | Product |
|---|---|---|
| `tests/presets/` | `A1_master`, `A3_filter`, `A4_amp`, `A5_pitch`, `A6_pattern`, `A7_envelope`, `A8_tail`, `A9_base`, `A9_hat.muRhythm`, `T11` to `T13.muRhythm`, `TS1`, `TS2` | mu-Clid |
| `tests/presets/` | `TONI_legacy`, `TONI_silent`, `TONI_mute_layer.muArp`; `ON_legacy`, `ON_silent` (the pre-format-2 shapes: a regression must still read them) | mu-Toni, mu-On |
| `d:\OneDrive\Documents\TDP\muClid` | all 10 `.muClid` full presets, a spread of about 15 of the 63 `.muRhythm` (all with samples, inserts, MIDI-out, each modulator kind), 2 `.muPerformance` | mu-Clid |
| `d:\OneDrive\Documents\TDP\muTant` | all 108 `.muTant`, about 20 of the 100 `.muPattern` (including Note-mode and user-wavetable ones), the performance files | mu-Tant |
| `...\muToni`, `...\muOn`, `D:\Andy Tuke\Music - Music Software\TDP\Presets` | whatever exists (the factory content of these two is empty in the folders seen: the factory state lives in code, so save a factory-state full preset from the baseline build), each product's `_default` | mu-Toni, mu-On, any |
| Host sessions | one `getStateInformation` blob per product per scenario of section 2 (`--save-state` from the baseline standalone, which is the exact bytes a DAW session holds) and, for mu-Clid and mu-Tant, an older-format session if one exists (pre-format-2 dump) | all four, plus mu-Clid MIDI (Lite session) |
| Clips | a full preset and a session with a populated `<Clips>` bank and an active clip, per product | all four |

Checks (one parameterised test per product, reusing `PresetMigrationTests` helpers):
1. **Loads without error** (`onLoadError` silent), and every parameter value read back equals a stored `params.json` dump taken by the baseline build.
2. **Re-save is byte-identical** to a stored `resaved.xml` produced by the baseline build (the saved text, not just the semantics), for full presets, layer presets, and sessions. Second save of the same instance is byte-identical to the first (idempotent). This is the strongest check that nothing in saved data changed (only mu-Tronic adds `type=`, and it is not in this corpus).
3. **Load then render equals the golden of the same state** (ties the corpus to section 2).
4. **Parameter manifest:** for each product, a frozen list of every parameter id, type, range, default and value-to-text sample from the baseline layout, asserted equal after each stage (catches a moved `addLayerParams` that drops, renames or re-types an id, including the shared `ch{N}_` strip and the `mstr_` globals). Cheap, runs in ms, highest value per line of test.
5. **Cross-product references:** a layer preset saved by the baseline loads into the extracted layer and re-saves unchanged (`.muRhythm`, `.muPattern`, `.muArp`, `.muTrack`), including `.muTrack` with each engine id.

### 5. Gates: pluginval, the listening suite, time cost

Time costs are estimates from the pipeline design, **not measured**; the first run of each will replace them in this table.

| Check | What it adds | Est. cost | Runs at |
|---|---|---|---|
| Static checkers (`tests/scripts/check-*.ps1`, plus the new `check-layer-boundary.ps1`) | boundary and include rules | seconds | every coding step |
| Per-product unit suites + the new golden / compat / equivalence tests | semantics, values, saved data | mu-Toni and mu-On under a minute; mu-Tant and mu-Clid a few minutes; goldens add about 10 s per scenario in Debug (est.) | after each stage's code, before review |
| `mu-core-tests` (and `mu-core-tests-16` from T0) | shared contract | about 2 min | every stage that touches mu-core, i.e. all |
| Listening suite, `--product <p>` (the `*_roundtrip`, swap and midi-clock tests of the touched product) | CLI path, preset and session round trip, swap timing | tens of seconds per product | each stage gate, for the product(s) touched |
| Listening suite, full (`run-listening-tests.py --config Debug`, 30 expectation files, Debug, 180 s timeout each) | all of the above, all four products | 10 to 20 min (est.) | before each commit of a stage that touches mu-core, and at the end of T4, T6, T5, T7; the audio-path rule in the test cadence applies (extraction is an audio-path change, so yes) |
| pluginval, `run-pluginval.ps1` strictness 5 (-SkipGui for speed) | VST3 hosting, state restore identity, parameter fuzz, many rates and block sizes, multi-instance | 5 to 15 min for 5 VST3s (est.); Debug GUI run has hung once for 53 minutes (monitor it with a log file and a verified kill) | the gates that touch mu-Clid and mu-Tant (live VST3 products): T5, T7, and the first mu-core stage that edits mixer audio code (T2); strictness 8 once at T7; mu-Toni / mu-On at the end of T4 / T6 if their VST3 exists. Not needed for mu-Tronic itself (Standalone only) |
| clap-validator (Release `.clap` only) | CLAP state and params | about 1 min each | T5, T7, release |
| Performance: CPU timing (new, section 6) | a refactor did not make `processBlock` slower | about 1 min | each gate of T4 to T7; load-sensitive, so run alone and compare ratios, not absolute numbers |
| Release build ± listening re-run | optimiser-dependent float differences | build minutes + the suite | only on Andy's release request |

### 6. Automatable real-time-safety checks (the net under the dsp-controller)

1. **Allocation counter.** The tests already count `operator new` on one thread (the mu-Clid MIDI parameter-listener test). Generalise it into `mu-core/Tests/AllocationProbe.h` (a global `operator new/delete` replacement in the test executable only, counting on the thread that sets an `AudioThreadScope`). Every golden and equivalence run asserts zero allocations and zero frees inside `processBlock`, `Layer::render` and `renderLayers`, including the first block after a swap, a type change and a stop. No production hook is needed.
2. **Lock probe.** Count acquisitions of `layersLock` and the bank lock on the audio thread (spin-lock try only): assert the audio thread never blocks (a worker holds the lock, the audio call returns within one block, silent output, transport still advanced, state identical afterwards). Exists in pieces (mu-Clid MIDI lock-skip, mu-Tant seam); extend to every product's layer type.
3. **Two-thread stress.** Layer edits (add, remove, swap, reset, type change, clip launch, staged swap, wavetable/sample load) from a worker against `processBlock` on the test thread for a fixed iteration count (not a time window), no crash, no NaN, no allocation, deterministic end state. Extends the `ProcessorSeamTests` pattern to the new `Layer::render` host. The stress also runs a **second "host" thread** that loops `prepareToPlay` / `releaseResources` against the 100 Hz service tick (it contends on `binMutex`), and a `prepareToPlay` that **grows the block size between `setLayerType` steps 2 and 8** (checked through the prepare-generation counter: the half-built layer must be rejected or rebuilt, never rendered with the old scratch size). Run once under ThreadSanitizer where available (the existing build-PC item; Windows MSVC has AddressSanitizer only, so TSan runs in the Linux CI job).
4. **Source scan, `tests/scripts/check-rt-safety.ps1`** (new, report-only first, then fail): in files reachable from `render`, `processBlock` and `renderLayers`, flag `new`, `std::vector::push_back/resize`, `juce::String` construction, `std::function` construction, `std::mutex`/`lock_guard`, `getParameter(...String...)`, `Time::` calls, `DBG`/`jassert` heavy formatting, `File`/`FileInputStream`, and a string-keyed APVTS lookup. It is a smell detector with an allow-list for audited lines (`rt-ok` marker with a reason), same style as `ref-ok` in `check-comment-refs.ps1`. It does not replace the dsp-controller.
5. **Denormal / NaN / inf / DC sweep.** Every golden scenario also asserts no NaN or inf, and the final 10 ms after a stop has no DC offset beyond a threshold; plus silence in, parameter extremes (all knobs min, all max), and sample rates 44.1, 48, 96, 192 kHz with block 1 and 4096 smoke renders per layer type (no hash, just finiteness and bounded peak).
6. **Retire bin / tail:** a type change or remove during a tail: the old layer is retired under the layer's `retirePolicy`, expressed in **seconds** (not blocks, so it does not depend on the host block size; this replaces the earlier `tailBlocks()` wording), output continuous (no step larger than 3x the control's, the `FxTailChecks.h` rule), the old layer freed on the message thread (asserted by a destructor-thread-id probe in a test layer type). Bin cases **R1 to R4** (design 2.1.3 invariants and the design 2.1.6 test list): **R1** a second `parkInPlace` on an active in-place row is a no-op; an in-place entry is skipped in every block whose view has its slot live, and on regrow (count first, then cut) no block has neither; a spare is never destroyed before its `cutSlot` (destructor probe, `addLayer` over a ringing spare); `swapRows` / `removeRow` cut in-place entries and move owned ones; **R2** `service`, a park and `cutAll` + `service` from a second "host" thread never overlap (`binMutex`, two-thread stress); `service` runs before the install in a deferred-work pass; **R3** both count policies (`RenderPastCount` renders above the count, `CutAtCount` cuts at the store) and every count store goes through the packed word (an `applyComposedState` race test); **R4** a failed `LayerBlock` changes no bin entry, an entry cut after phase 0 renders silence in its slot, and the activation serial gives the same-block hand-over, including across a serial wrap. Test names quote the R number.
7. **CPU budget:** 16 layers (T3 onward, fake and real types) and each single product's 8 layers, timed over 1000 blocks, asserted at or below `1.15 x` the baseline ratio of render time to the unextracted path in the same process (the switch of section 3 gives the comparison for free).

### 7. Tests to write before the first extraction stage (T3/T4), in order

Effort in focused work (test steward), including running them; builds are the main conversation's.

| # | Test | Needs from the main conversation | Effort |
|---|---|---|---|
| 1 | **Parameter-id manifest** per product (section 4.4) + frozen dumps | baseline build (no source change) | 0.5 day |
| 2 | **Compat corpus** collected, `MANIFEST.txt`, load + re-save byte-identical + values equal (section 4.1 and 4.2) | none | 1 day |
| 3 | **GoldenRender.h** (scenario loader, fingerprint, equality rule, diff dump, self repeat test) and `update-golden.ps1` | none | 1.5 days |
| 4 | **First goldens, mu-Toni then mu-On** (closest to the first stages): about 10 scenarios each, noise off, record at baseline | none | 1 day each |
| 5 | **Goldens, mu-Tant then mu-Clid** (live products; needed before T5/T7 but cheap to record now while the baseline is clean) | none | 1.5 days each |
| 6 | **AllocationProbe.h** and wiring it into the golden runs | none | 0.5 day |
| 7 | **mu-On per-engine tests** (Bass, Snare, Rumble sound + decay, value-level Kick/Hat), global swing/accent, Rumble source read, bass-to-kick sidechain: pin the behaviour T6 changes | none | 1 day |
| 8 | **mu-Toni `readVoice`, held-note, root-by-MIDI, MIDI-trigger** tests (pin what T4 moves per layer) | none | 0.5 day |
| 9 | **mu-Tant Note-mode pin** (stem and with-FX final mix; documents the old behaviour before T5) | none | 0.5 day |
| 10 | **LayerEquivalence.h** (section 3) | the `useLayerRender` switch and the channel tap decision | 1 day, after T3 exists |
| 11 | **Mixer block-value test** in `mu-core-tests` (render a known source through `MixerEngine`; level, pan, sends, sidechain, mute/solo with exact expected values) | none | 1 day |
| 12 | **Lock probe and two-thread stress** generalised (section 6.2 and 6.3) | none | 1 day |
| 13 | `check-rt-safety.ps1` (report-only), `check-layer-boundary.ps1` fixture (the architecture-steward's checker needs a fixture set of its own, as `test-check-core-boundary.ps1` has) | the checker script | 0.5 day |
| 14 | CPU budget test (section 6.7) | the switch of #10 | 0.5 day |
| 15 | Seed fix request for mu-Tant `NoiseGen` and mu-Toni `rng` (source change, main conversation) then noise-on goldens + a statistical noise test | source change approved | 0.5 day after the change |
| 16 | **Contract pin P1** (mu-Tant): shrink then regrow within 25 ms; shrink-regrow-shrink; two count-reducing swaps back to back dropping the same voice | none (recorded on the unmodified tree) | 0.5 day |
| 17 | **Contract pin P2** (mu-Clid): the blocks rendered after `isFullyDrained()` at the harness's deferred-work rate, and their peak | none | 0.25 day |
| 18 | **Contract pin P3** (mu-Clid): a count-reducing full preset while tails ring, then a regrow; `removeLayer` while tails ring | none | 0.5 day |
| 19 | **Contract pin P4:** the longest retired tail in the golden set, `ampRelToEnd` included (30 s cap) | goldens of items 4 and 5 | 0.25 day |
| 20 | **Contract pin P5** (mu-On): the stop edge with `applyParams` before `reset`, and a 0-frame stop block | none | 0.25 day |
| 21 | **Contract pin P6:** a structural edit mid-play in mu-Tant, mu-On and mu-Clid, recording the note state after the missed blocks (no note release; this is the R5 false-jump fix) | none | 0.5 day |
| 22 | **Contract pin P7:** one oversize-block segment per product (re-recorded at each product's adoption of the driver, in a signed goldens-only commit) | goldens of items 4 and 5 | 0.25 day |
| 23 | **Measurement M1:** block-to-block host position deltas logged under fast tempo automation (Bitwig, Reaper) and under MIDI clock with the sync-offset ramp; sets the jump tolerance | Andy at a DAW | 0.5 day, manual |
| 24 | **Measurement M2:** Debug pluginval for VST3, CLAP and Standalone with the `AudioEpoch` parity assert, plus offline bounces while playing in Reaper and Bitwig | Debug build, Andy at a DAW | 0.5 day |

Sequencing of the contract pins and measurements (extraction-plan E0.6): **P1 before E4.6; P2, P3 and P4 before E5.4(d); P5 before E3; P6 before E1's consumers; P7 before each product adopts the driver; M1 before T1; M2 before E4.** All are recorded on the unmodified tree, so they describe today's behaviour; a later change to any of them is a deliberate, signed update.

Roughly 12 to 13 working days of test work; items 1 to 6 (about 6 days) are the minimum before **any** code moves; 7 to 9 before the stage they pin; 10 to 14 before the first gate. The suggested sequencing is items 1, 2, 3, 6, 4 (mu-Toni), 8, then T4 can start while 4 (mu-On), 7, 11 continue; T6 starts only after 7; T5 and T7 start only after 5, 9 and a clean full-suite baseline.

**What the dsp-controller should focus on at each gate.**
- **T3 (`Layer::render`, `RenderContext`, `renderLayers`, `RetireBin`):** the one try-lock round `layersLock`; scratch buffers sized in `prepareToPlay` and never grown; lock miss still advances transport and boundary detection; retire-bin handoff (atomic publish, free off the audio thread, no ABA on slot reuse); priority-1 order; empty layers skipped; no allocation under the lock; `RenderContext` is trivially copyable with no pointers outliving the block. From the round-1 contract review: **R5** no false jump after a missed or edit block (the position delta over missed blocks is not read as a loop wrap or a transport jump); **R6** `startBeat` of -4 (count-in) and `wrapBeat`; **R7** the tail MIDI sink (note-offs from a retiring layer reach the output); the `AudioEpoch` parity and timeout cases; the `RenderContext` padding freeze (a size/offset static check so padding bytes cannot carry state); and a **twin test that scrambles ALL APVTS parameters before the strip** (N6: two layers built from the same state, one with every parameter randomised first, must render identically once the strip is applied, proving nothing reads a stale parameter).
- **T4 (mu-Toni):** `readVoice` atomic pointer table bound once on the message thread and not dangling after a layer move; held-note stack per layer is lock-free or audio-thread-only (the MIDI buffer is consumed on the audio thread); the engine inside the layer (a `unique_ptr`) and the pointer-move semantics; ToniVoice noise seeding; first block after swap.
- **T6 (mu-On):** per-layer engine allocation and engine swap on the message thread with a staged, retired old engine (no engine allocated on the audio thread; the five-engine pool going means the old "all engines ready" guarantee goes); per-track grid and clocks derive from `RenderContext::startBeat` without drift; Rumble's read of another layer's output after it is rendered (render order, silent when the source is empty/removed/priority-1); bass to kick sidechain still reads the right envelope.
- **T5 (mu-Tant, live):** the 300-line render chain's filter/pitch envelope state across the move; retire fade replaced by the shared bin (no tail difference: the golden); wavetable bank lock shared between two product users (no priority inversion, try-lock only on the audio thread); Note-mode gate ramp: where it multiplies, denormals, click on release; `swapLoopBeats` to `loopBeats` unchanged wrap detection including non-divisor lengths.
- **T7 (mu-Clid, live, highest risk):** the Stage 7 proof list in full; `SampleRhythm` engine/MIDI engine/play state lifetime across hot-swap; sample I/O only in `preloadStaged`; retired-tail drain and cleanup flags; MIDI-out event ordering and orphan note-offs on stop and swap; modulation seed and write-back (the 444-line glue) per block with no string lookups; `HotSwapStager` timing equal to before.
- **Every gate:** the first block after a swap or type change, the stop edge, 1-sample blocks, denormals in tails, and that no stage changed arithmetic order (the golden says so; the dsp-controller reads the diff for reordering the golden cannot see, e.g. at a different sample rate).

### 8. Rules for the net itself

- A stage is not closed while any golden, compat, equivalence or manifest test is red, unrun, or has had its reference edited in the same commit.
- Unrun tests are recorded as **unrun** here, never as passing. Result counts are filled in below as the tests are written and run.
- Baseline commit for all references: to be fixed when item 3 starts (must be a clean tree on a commit where all current suites pass; today HEAD d385df9 has uncommitted site changes only).

| Item | Status |
|---|---|
| Parameter-id manifests | Planned, not written |
| Compat corpus + re-save check | Planned, not collected |
| `GoldenRender.h`, `update-golden.ps1` | Planned, not written |
| Goldens: mu-Toni, mu-On, mu-Tant, mu-Clid, mu-Clid MIDI | Planned, not recorded |
| `AllocationProbe.h`, lock probe, two-thread stress | Planned, not written |
| `LayerEquivalence.h` | Planned (needs T3 and the path switch) |
| mu-On engine, mu-Toni `readVoice`, mu-Tant Note-mode, mixer block-value pins | Planned, not written |
| `check-rt-safety.ps1`, `check-layer-boundary.ps1` fixtures | Planned, not written |
| Contract pins P1-P7, measurements M1, M2 | Planned, not written |
| Golden harness (`GoldenRender.h` and per-product `GoldenRenderTests.cpp`), branch `e0-harness`, commits 319cbb3, 7ad0466, 123fc59 | Built on branch, 482 mu-core results, not yet merged. Deviation: a scenario with `tolerance_abs` set compares only the per-segment peaks (not the max per-sample difference described in section 2), so the tolerance is a peak bound; keep `tolerance_abs` at 0 until the comparison is extended |
| E0.2 seed fix (mu-Tant `NoiseGen`, mu-Toni `rng`), branch `e0-seed-fix`, commits 4605cb6, d353ff1 | Signed (dsp-controller Pass with notes), not yet merged. mu-Tant 184 and mu-Toni 62 results. Open note: `BitcrusherInsert` and `VocoderInsert` still seed per slot without a per-instance salt (pre-existing); to be done with the shared contract |

**Golden fingerprint: -0.0 versus +0.0 (decided by the test steward).** They are **different**. The fingerprint hashes the raw IEEE-754 bit patterns and the equality rule is bit-identical (tolerance 0), so a changed sign of zero is a changed result. The harness must not normalise zeros before hashing. Reason: extraction stages move code and must not change arithmetic; a flipped zero sign is the signature of a reordered or re-expressed operation (for example `0.0 * -x`, or a gain applied before rather than after a sum), which is exactly what the net exists to catch, and it costs nothing to keep because a pure move produces none. To keep a mismatch easy to read, the diagnostic on a failing segment should say when the only differing samples are zeros of opposite sign ("sign of zero only, inaudible, still a failure"); a deliberate golden update with the dsp-controller's signature is the way to accept one. The tolerance mode (peaks only) is unaffected, since a sign of zero cannot change a peak.

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
