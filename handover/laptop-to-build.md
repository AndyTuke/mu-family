# Laptop → build PC

Written only by the laptop. The build PC reads it and answers in [build-to-laptop.md](build-to-laptop.md). Protocol: [README.md](README.md).

Next id: L-012

## Status (laptop)

- Written: 2026-10-30 (updated after the audit batch). Everything is pushed to `origin/main`; handover files follow the last source commit.
- Nothing written on the laptop since the 1454 build has been compiled or run. The source below was written without a compiler.
- Needs the build PC: only what is listed under Open.

## Open

### L-001 · 2026-10-30 · Build everything and test the mu-link slot-lifecycle fix (protocol v5)
Rows: #1703 · Commits: 0a335b69, b6df7df2 (review fixes), docs in design-mulink.md · Needs: build, listen
Do: pull `origin/main`. Full Debug build of ALL products including mu-link (protocol v5 refuses a v4 peer, so mu-link and every Standalone must be rebuilt together; this increments the build number).
Do: build and run the mu-link tests (SharedMemoryTests: stale audio not inherited, unique tokens, stale owner cannot free or write into the new tenant's slot; ServerTests: freed slot drained; ClientTests). Fix any compile error: the code was written without a compiler.
Do: two apps attached to mu-link, then detach and re-attach one with a single free slot; repeat with the mu-link audio device stopped. Listen for a new app playing the previous app's audio.
Report: build number, test result, anything heard, whether a freed slot became claimable again; then close or update #1703 with the backlog-administrator (the row also lists remaining review notes).

### L-002 · 2026-10-30 · Run the paging tests and smoke row MS2 (mu-On and mu-Tronic Steps box)
Rows: #1681 (and #1712 tokens, #1701 helper idea) · Commits: 3ed27f4c · Needs: build, look
Do: `cmake --build build --target mu-on-tests --config Debug` then run `mu-on-tests Sequencer` (8 new cases in StepPagingTests.cpp, added to mu-on-tests CMake, written without a compiler).
Do: run smoke row MS2 in tests.md in both mu-On and a mu-Tronic Track layer: Prev/Next and the Page n / N label, page falling back when Length shrinks, hatching past the Length, selected step and Step panel refresh, tie stub at a page start, playhead following into the shown page, mu-On and mu-Tronic behaving the same.
Do: regenerate both manuals (docs/mu-on/create_manual.ps1, docs/mu-tronic/create_manual.ps1), run `docs/export-manual-pdfs.ps1`, retake mu-On main-window.png, ghost-steps.png, rumble-track.png and mu-Tronic main-window.png (optionally add one of the paging strip with hatched steps).
Report: test output, MS2 result, manuals regenerated; the test-steward sign-off on #1681 follows once MS2 and the tests pass.

### L-003 · 2026-10-30 · Look at generated textures at 150% and 200% display scale
Rows: #1709 · Needs: look
Do: run any product at 100%, 150% and 200% Windows display scale and look at the brushed-metal panels and the knob shadows for softness (brushedGrain in MuLookAndFeel.cpp is cached at logical size and may be built too small).
Report: soft or sharp at each scale, with a screenshot if soft; that decides whether #1709 is a real defect.

### L-004 · 2026-10-30 · Confirm the layer-boundary check
Rows: #1713 · Needs: build
Do: run `pwsh tests/scripts/check-layer-boundary.ps1`; it should report exactly one violation (mu-on TrackParams.cpp including Plugin/MixerFxParams.h). It was last run on the laptop.
Report: the output.

### L-005 · 2026-10-30 · Verify two audio-thread fixes written without a compiler, and re-run the rt-safety scan
Rows: #1704 (ModulationMatrix key allocation), #1715 (hot-swap while bypassed) · Commits: 850a90ff · Needs: build, listen
Do: build all products (together with L-001's build). Compile errors in mu-core/Modulation/ModulationMatrix.h/.cpp (new `outputKeyScratch`, reserved in rebuildCache) and mu-core/Plugin/ProcessorBase.h (new public `processBlockBypassed` override calling `resetSwapPlayState()`) are the first thing to look for.
Do: run mu-core's unit tests (ModulationMatrix and HotSwap suites). Add or run a test that renders a ModulationMatrix whose control sequence has a 12-character id under an allocation counter (no allocation on the render path), and a processor-level check that a preset loaded while the host reports the plugin bypassed applies at once (HotSwapTests covers only the swapper).
Do: in a host (or mu-On/mu-Tant standalone with a bypass), load a preset while bypassed and confirm it applies without un-bypassing; then un-bypass and confirm nothing jumps.
Do: run `pwsh tests/scripts/check-rt-safety.ps1`; expect 0 findings outside setup context (132 in setup, 104 suppressed; it was 342 before the script was retuned and the code marked) and exit code 0; note any difference.
Report: build result, test output, what was heard in the bypass check, the rt-safety count; then the backlog-administrator can close #1715 (and update #1704).

### L-006 · 2026-10-30 · Verify the Karplus-Strong gain limit (written without a compiler)
Rows: #1714 · Commits: 1b15c987, 273737ce · Needs: build, listen
Do: build (with L-001/L-005). Changes: new `mu-core/Audio/SoftLimit.h` (`mu_dsp::softLimit`), used by `KarplusStrongInsert.h` (soft limit on the feedback term only, so 0 % feedback stays an exact passthrough) and by `VocoderInsert.h` (its private copy now delegates).
Do: run mu-core tests, especially `SpectralBehaviourTests` (Karplus pitch stays on the note, Vocoder stays in range) and `InsertDSPSmokeTests`; a Karplus golden or level test may need a deliberate update (pre-release: sound changes are fine, but record that it moved). Add a test: a 1 kHz sine and a DC offset through the insert at feedback 75 % and 100 %, assert the resonant part stays bounded (output peak <= input peak + 1).
Do: listen to the insert at 75 % and 100 % feedback on a sustained note and on a plucked input; check it still sounds like a string and does not clamp harshly.
Report: build result, test output, what was heard; then #1714 can be closed (the dsp-controller should re-check the limiter placement).

### L-007 · 2026-10-30 · Verify the insert tone-filter state fix (written without a compiler)
Rows: #1702 (InsertProcessor stale tone-filter state while bypassed) · Needs: build, listen
Do: build (with L-001). Changes: `OnePoleLP::setState` in `mu-core/Audio/AudioFilters.h`; `InsertProcessor.cpp` copies the block's last sample into `postDriveTone` whenever the post-drive tone is not applied (tone open at 19 kHz or more, or an algorithm outside 1-5); `OverdriveInsert.h` follows the signal per sample while its tone is open.
Do: run mu-core tests (InsertDSPSmokeTests and any insert golden). Add a test: drive a steady signal, toggle the tone cutoff across 19 kHz mid-signal, assert no discontinuity (sample-to-sample step) at the switch.
Do: listen: sweep the tone knob of a drive insert (e.g. Overdrive) across the fully-open end while a note sounds; there should be no click.
Report: build result, test output, what was heard.

### L-008 · 2026-10-30 · Verify the duplication lifts written without a compiler (#1701 items 1, 3, 4, 5 and the step clamps)
Rows: #1701 (plan: docs/refactor-plan-2026-10.md) · Needs: build, run tests
Do: focused builds of mu-Clid, mu-Tant, mu-Toni, mu-On (and mu-Tronic, which links mu-On's layer library), then the full Debug build with L-001. All changes are behaviour-preserving moves; goldens must NOT change. Fix compile errors first: this was written without a compiler.
Changes to check: (1) new `mu-core/Modulation/LastResolved.h` (`mu_mod::LastResolved<N>`, included by `LaneModulation.h`) replacing the hand-copied hold-last-value block in mu-Tant `Pattern.cpp/.h`, mu-Toni `Arp.cpp/.h` and mu-On `Track.cpp/.h` (mu-On keeps its per-engine `lastResolvedEngine`). (3) `WavetableBank::indexForFile(file, lock)` in mu-core, now the body of mu-Tant `PluginProcessor::bankIndexFor` and `PatternType::bankIndexFor`. (4) new `mu-core/Modulation/ParamSkew.h` (`mu_mod::skew::propFromCutoff/cutoffFromProp`, named constants) used by mu-Tant `Pattern.cpp` and by mu-Clid `ModulationSkew.h` (whose cutoff pair now delegates). (5) new `mu-core/Control/ParamActual.h` (`mu_core::setParameterActual`) and mu-Clid `UI/Voice/RhythmParamCache.h` replacing four copies of `apvtsSet` (Amp/Filter/Pitch subsections, MidiOutSection) and the channel-send write in AmpSubsection. (8, partial) new `mu-on/Source/UI/StepPaging.h` (`clampPage`, `clampStep`) used by `StepPager.cpp` and `TrackContent.cpp`. Also `RumbleEngine.h` gained `jassert(n <= maxBlockSize)`.
Do: run mu-Clid `ModulationSkewTests`, mu-Tant tests and goldens (`SynthDSPTests`), mu-Toni tests, `mu-on-tests Sequencer` (new `clampPage` / `clampStep` cases in `StepPagingTests.cpp`), and the daily golden sweep.
Note: mu-Toni's Arp never reset its held values on prepare or bind (mu-Tant and mu-On do); the lift kept that behaviour. Decide whether mu-Toni should reset like the others.
Report: build result, test output, any golden that moved.

### L-009 · 2026-10-30 · Approved build-PC sequence after the verification items (Andy accepted the refactor plan D1-D6)
Rows: #1699, #1701, #1705, #1716 · Plan: docs/refactor-plan-2026-10.md · Needs: build, code
Do (after L-001 to L-008 pass): Day 1: #1699 phase A (split `ProcessorBase.cpp` into topic files; code moves only, header untouched; fix forward). Days 2-4: mu-Tilate step 1, lift mu-Clid's sample-library mechanism (`SampleLibrary`, `SamplePreview`, `SampleBrowser`, `embedded_sample`, the path resolver) into mu-core with a per-product folder setting, mu-Clid refactored onto it, no duplicate. Day 5: #1699 phases B and C (`FxParamSync`, `ProgramChangeRouter`, `ActivationWorker`). Days 6-8: the Signalsmith Stretch prototype (latency hiding via Hold + seek, varying-rate drive, real CPU and memory; dsp-controller review; no refactoring on those days). Days 9-10: #1701 item 8 (step-edit state machine wired into `StepRows`, `TrackStepRow` and the mu-Tronic mirror) with #1699 phase J. Decided: stop at phases A to E; dsp-controller review required for #1701 item 2 and phases G/H; #1701 item 7 accepted as debt; item 6 waits; main checkout, no worktrees.
Report: what landed, build number, tests, anything that moved.

### L-010 · 2026-10-30 · mu-Toni Arp should reset its held modulation values like mu-Tant and mu-On
Rows: #1701 · Needs: code, test
Do: in mu-Toni `Arp` call `lastResolved.reset()` on prepare, bindParams, a slot move/reset and going live (as mu-Tant `Pattern.cpp` and mu-On `Track.cpp` do), so values held for a matrix-lock miss never cross a slot. Add a test that a lock-miss block right after a bind keeps the unmodulated values, not the previous slot's.
Report: build and test result.

### L-011 · 2026-10-30 · mu-Tant block-rate modulation and gate edge clicks, with Andy listening
Rows: #1716 · Needs: code, listen, decide
Do: ramp the per-block pitch/filter envelope and pitch modulation across the block (or smooth `osc*SemiMod`); slew the gate's falling edge with a 1-2 ms release and run `GateMode::Silence` through the same gater. Render one pattern at block sizes 64, 512 and 2048 and compare spectra; listen. Pre-release: sound changes are fine, update the goldens deliberately and record that they moved.
Report: what changed audibly, which goldens were updated.

## Results

None yet.
