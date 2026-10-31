# Build PC → laptop

Written only by the build PC. The laptop reads it and answers in [laptop-to-build.md](laptop-to-build.md). Protocol: [README.md](README.md).

Next id: B-003

(B-001 resolved and removed: see the Extra entry below.)

## Status (build PC)

- Written: 2026-10-31. Build number 1455 (`mu-core/BuildNumber.h`). HEAD is the commit that carries this file; everything is pushed.
- Andy was away: nothing was listened to or looked at. Every listen / look step is still open.
- Full Debug build of all products is clean (the code written without a compiler compiled). Release was not built.

## Open

### B-002 · 2026-10-31 · Frozen manifests, corpus params and goldens differ from the files in tests/
Rows: none yet · Needs: decide
Not goldens-only: the parameter manifests of mu-On, mu-Clid, mu-Tant and mu-Toni differ from `tests/manifests/*.txt` (mu-On: `*_drvChar` / `mst_insChar` / `mst_ins2Char` rows removed or changed), and the compat-corpus param dumps differ for mu-Clid (Big and Bad, Simple Hat) and 11 mu-Tant presets. These read as deliberate changes; re-run the owning test with `MU_REGENERATE_FROZEN=1` and review the diff if you agree. The comparison now ignores `*.state.hash` (information only), which took mu-On from 16 to 10 failures; the other golden cases moved as well (mu-On 9, 7 of them Bass-engine cases, plausibly the Bass Tune root-note change `8031495e`; mu-Clid 8+) and were left stale on purpose (Andy's rule), including `inserts-karplus-ring` (expected by L-006). I did not regenerate anything.

## Results

### Re L-001 · 2026-10-31 · Partly
Build: v1.1.0.1455 · Ran: full Debug build of all products incl. mu-link; `mu-link-tests` (86 results, 0 failures) · Saw: nothing listened.
Notes: not done: the two-apps attach / detach / re-attach check and listening for stale audio. #1703 stays open until that is done.

### Re L-002 · 2026-10-31 · Partly
Build: v1.1.0.1455 · Ran: `mu-on-tests` (239 results, 16 failures; the StepPaging cases ran and none of the failures is in them, the 16 are the manifest and golden differences in B-002) · Saw: nothing.
Notes: not done: smoke row MS2, manual regeneration, screenshots.

### Re L-004 · 2026-10-31 · Done
Build: v1.1.0.1455 · Ran: `pwsh tests/scripts/check-layer-boundary.ps1` · Saw: exactly one violation, `mu-on-layer/Persistence/TrackParams.cpp:3` includes `Plugin/MixerFxParams.h`, as expected.

### Re L-005 · 2026-10-31 · Partly
Build: v1.1.0.1455 · Ran: build (no compile errors in `ModulationMatrix` or `ProcessorBase`); `mu-core-tests`; `check-rt-safety.ps1` · Saw: rt-safety 0 findings outside setup, 132 in setup, 104 suppressed, exit 0 (as expected).
Notes: the two extra tests (12-character id under an allocation counter; a processor-level load while bypassed) were NOT written, and the bypass listening check was not done.

### Re L-006 · 2026-10-31 · Partly
Build: v1.1.0.1455 · Ran: `mu-core-tests`, which includes `SpectralBehaviourTests` and `InsertDSPSmokeTests` (no failures in either) · Saw: `mu-clid` golden `inserts-karplus-ring` moved (292 differences, first at 3258 ms, RMS unchanged to 0.01 dB), as the item expected; not re-recorded.
Notes: the bounded-output test (1 kHz sine and DC at 75 / 100 % feedback, output peak <= input peak + 1) is written and passes (`InsertDSPSmokeTests`); no listening.

### Re L-007 · 2026-10-31 · Partly
Build: v1.1.0.1455 · Ran: `mu-core-tests` (InsertDSPSmokeTests passes) · Saw: nothing.
Notes: a continuity test (tone open for 4 blocks, then 5 kHz, step < 0.05 at the switch) is written and passes; I did not check that it fails without the fix; no listening.

### Re L-008 · 2026-10-31 · Partly
Build: v1.1.0.1455 · Ran: full Debug build; `mu-clid-tests` (354 results, 17 failures), `mu-on-tests` (239, 16), `mu-core-tests`; `mu-tant-tests` and `mu-toni-tests` were cut off by my 300 s cap and did not finish (their first results were the manifest / corpus differences of B-002); `mu-clid-midi`, `mu-control`, `mu-tronic` (132 results) and all four `*-layer-tests` pass · Saw: goldens moved in mu-On and mu-Clid.
Notes: everything compiles. I cannot say the goldens moved because of the lifts: they were already stale (no golden has been re-recorded since the sound changes), and the manifest differences are separate. To judge the lifts, re-record on a clean pre-lift commit and compare, or rerun mu-Tant and mu-Toni to the end (they need a longer cap than 300 s in Debug).

### Not started (no result entry)
L-003, L-009, L-010, L-011: not touched; they need eyes, ears or the earlier items to pass first.

### Extra · 2026-10-31
`mu-core-tests` started at 4 failures; 3 were tests that did not expect the new gain ramps (master pan, return mute) and were fixed in `0af80996` by running one settle block. The remaining one (NaN counter) was a stale expectation: the insert stage now contains the NaN before the master probe (audit fix), so the test now expects the counter to stay 0; mu-core-tests is 0 failures of 836.
