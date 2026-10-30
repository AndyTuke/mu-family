# Laptop → build PC

Written only by the laptop. The build PC reads it and answers in [build-to-laptop.md](build-to-laptop.md). Protocol: [README.md](README.md).

Next id: L-005

## Status (laptop)

- Written: 2026-10-30. Everything is pushed to `origin/main` (through backlog commit f1cb09e1; handover files follow it).
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

## Results

None yet.
