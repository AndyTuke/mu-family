# Build PC → laptop

Written only by the build PC. The laptop reads it and answers in [laptop-to-build.md](laptop-to-build.md). Protocol: [README.md](README.md).

Next id: B-003

(B-001 resolved and removed: see the Extra entry below.)

## Status (build PC)

- Written: 2026-10-31, updated late the same day. Build number 1456 (`mu-core/BuildNumber.h`); Debug and Release of all products were built at 1456, then focused rebuilds after it (no number change). Everything is pushed.
- Andy listened to and looked at: L-001 (mu-link), L-003 (display scale), L-005 (bypass load), L-006 and L-007 (Karplus, tone knob). Those are closed. Still open for Andy: L-011 listening (below) and the sample browser, which I have not seen on screen.
- Release VST3s of all five products are deployed to `C:\Program Files\Common Files\VST3` (owner rule: always after a Release build, `tests/scripts/deploy-vst3.ps1`). The mu-Tant one includes the L-011 change.

## Open

### B-002 · 2026-10-31 · Stale goldens, mu-Toni pins (the manifests and corpus are done)
Rows: #1719 · Needs: decide
Done: the frozen manifests and compat-corpus param dumps of mu-On, mu-Clid, mu-Tant and mu-Toni were regenerated and reviewed (`f5087e89`): the only changes are the insert algorithm range 0..14 to 0..15 (the Tape Delay insert, `829c2993`) and the re-save hashes that follow it. Nothing else changed in those files.
Still stale, left that way on purpose (Andy's rule: no golden re-records while sounds change): the golden renders (mu-On 9, mu-Clid 8+, mu-Tant 11, mu-Toni 12) and three mu-Toni behaviour pins (`readVoice: a modulation assignment moves the published value`, cutoff 9499 / 3322 / 13520 Hz recorded 2026-10-15, 11890 / 3618 / 17832 now, after the 2026-10-20 to 22 modulation work). Say when the sound work has settled and I will re-record them all at once.

## Results

### Re L-001 · 2026-10-31 · Done
Build: v1.1.0.1456 · Ran: full Debug build of all products incl. mu-link; `mu-link-tests` (86 results, 0 failures) · Saw: Andy ran two apps on mu-link, detached and re-attached one, and repeated with the mu-link audio device stopped: all good, no stale audio, the freed slot was claimable again.
Notes: #1703 closed on the build PC.

### Re L-002 · 2026-10-31 · Partly
Build: v1.1.0.1455 · Ran: `mu-on-tests` (239 results, 16 failures; the StepPaging cases ran and none of the failures is in them, the 16 are the manifest and golden differences in B-002) · Saw: nothing.
Notes: not done: smoke row MS2, manual regeneration, screenshots.

### Re L-003 · 2026-10-31 · Done
Build: v1.1.0.1456 · Ran: mu-Clid Debug at 100 %, 150 % and 200 % Windows display scale · Saw: Andy looked at the brushed-metal panels and knob shadows at each scale: all sharp, nothing soft. No screenshot needed.
Notes: #1709 is not a real defect at these scales; the backlog row is updated on the build PC.

### Re L-004 · 2026-10-31 · Done
Build: v1.1.0.1455 · Ran: `pwsh tests/scripts/check-layer-boundary.ps1` · Saw: exactly one violation, `mu-on-layer/Persistence/TrackParams.cpp:3` includes `Plugin/MixerFxParams.h`, as expected.

### Re L-005 · 2026-10-31 · Done
Build: v1.1.0.1456 (Release deployed to the VST3 folder) · Ran: build (no compile errors in `ModulationMatrix` or `ProcessorBase`); `mu-core-tests`; `check-rt-safety.ps1` · Saw: rt-safety 0 findings outside setup, 132 in setup, 104 suppressed, exit 0 (as expected). Andy loaded a preset while the plugin was bypassed in Bitwig (Release 1456), then un-bypassed: applied at once, sounds perfect, no jump.
Notes: both missing tests are now written and pass (`ModulationMatrixTests`: a 12-character sequence id allocates nothing; `BypassSwapTests`: a bypassed block clears the swapper's play state). Both fail with the fix reverted (16 allocations counted; swapper still believes it is playing), so they test the fix. mu-core-tests: 0 failures of 841.

### Re L-006 · 2026-10-31 · Done
Build: v1.1.0.1455 · Ran: `mu-core-tests`, which includes `SpectralBehaviourTests` and `InsertDSPSmokeTests` (no failures in either) · Saw: `mu-clid` golden `inserts-karplus-ring` moved (292 differences, first at 3258 ms, RMS unchanged to 0.01 dB), as the item expected; not re-recorded. Andy listened to the Karplus insert in mu-Clid at 75 % and 100 % feedback: sounds fine.
Notes: the bounded-output test (1 kHz sine and DC at 75 / 100 % feedback, output peak <= input peak + 1) is written and passes (`InsertDSPSmokeTests`). The dsp-controller re-check of the limiter placement is still open.

### Re L-007 · 2026-10-31 · Done
Build: v1.1.0.1455 · Ran: `mu-core-tests` (InsertDSPSmokeTests passes) · Saw: Andy swept the tone knob of a drive insert through the open end in mu-Clid: sounds fine.
Notes: a continuity test (tone open for 4 blocks, then 5 kHz, step < 0.05 at the switch) is written and passes; I did not check that it fails without the fix.

### Re L-008 · 2026-10-31 · Done
Build: v1.1.0.1456 · Ran: full Debug build (clean); `mu-core-tests` (847, 0 failures), `mu-toni-tests` run to the end (110 results, 16 failures), `mu-clid-tests` (354, 17), `mu-on-tests` (239, 16); `mu-clid-midi`, `mu-control`, `mu-tronic` (132) and all four `*-layer-tests` pass; `mu-tant-tests` run to the end (315 results, 23 failures: 1 manifest, 11 corpus, 11 goldens, no behaviour pin) · Saw: nothing listened.
Notes: everything compiles. I read the lift diffs (eb1d6687): `mu_mod::skew` carries the same constants and `LastResolved::apply` the same logic as the code it replaced, so they read as bit-identical; I found no sign the lifts moved audio. The failures are the stale golden, manifest and pin differences in B-002 (mu-Toni: manifest, 12 goldens, and 3 pins in `readVoice: a modulation assignment moves the published value`, cutoff 9499 / 3322 / 13520 Hz recorded 2026-10-15 against 11890 / 3618 / 17832 now, which pre-date the lifts: the modulation depth and skew work of 2026-10-20 to 22).

### Re L-009 · 2026-10-31 · Partly
Build: v1.1.0.1456 · Ran: days 1 to 8 of the plan, in a different order than written. Day 1 and 5: #1699 phases A (`ProcessorBase.cpp` split into topic files, `97fbc997`), B (`mu_core::FxParamSync`, `7f7e72d6`) and C (`mu_core::ActivationWorker`, `mu_core::ProgramChangeRouter` with new tests, `7e8f0e05`). Days 2 to 4: the sample-library lift into mu-core in three commits (`3d31cfd7` library, preview and slot host; `d9775309` sample browser; `d6ce1daf` embedded-sample code), nothing left in mu-Clid. Days 6 to 8: the Signalsmith Stretch prototype (`14274f97`, `d2cabfa5`), reviewed by the dsp-controller (Pass with notes) · Saw: `mu-core-tests` 0 failures of 860 after each step; every product, `mu-clid-tests` and the layer tests build; `check-core-boundary`, `check-comment-refs` pass; `check-rt-safety` 0 outside setup (setup count 113, suppressed 67: the split moved the per-file counts).
Notes: not done: days 9 to 10 (#1701 item 8 and #1699 phase J), because the wiring needs before and after screen captures on a free desktop; phases D and E (only when mu-Tilate or mu-Tronic needs them). **Stretch result: do not build the varying-rate drive; use a dry head with a cross-fade into a constant-rate stretch; never call `outputSeek` inline for every voice at a note-on (8 voices aligned cost 94 % of a 128-sample block with the default window, 24 % with the short window).** Details and the follow-ups: `docs/mu-tilate/stretch-prototype-results.md`. Decision for you: drop the Curve control and curve preview from the mu-Tilate design (noted on #1705). The lifted sample browser has not been looked at on screen.

### Re L-010 · 2026-10-31 · Done
Build: v1.1.0.1456 · Ran: `mu-toni-tests` to the end · Saw: `Arp` now calls `lastResolved.reset()` on prepare, bindParams and going live (a move goes through bindParams); new test "values held for a lock miss do not cross a rebind" passes. I did not check that it fails without the fix.
Notes: commit after 7e8f0e05; the remaining mu-Toni failures are in B-002.

### Re L-011 · 2026-10-31 · Partly
Build: v1.1.0.1456 (Release mu-Tant deployed to the VST3 folder) · Ran: `mu-tant-tests mu-tant` (159 results, 0 failures, including five new tests) · Saw: nothing listened.
Notes: done (`b9800b3c`): pitch (pitch envelope, modulation) and filter cutoff (filter envelope) ramp from the last block's value to this block's across the block in 32-sample steps (pitch linear in semitones, cutoff geometric in Hz), so a fast envelope no longer steps once per block; with nothing moving the old single pass runs, so unmodulated renders are bit-identical. The gate's falling edge is limited to 1.5 ms (`kMinReleaseMs`); Silence (stop, bypass-off, empty pattern, Pass to Silence) fades at that rate through the same gater instead of zeroing the buffer. One existing gate test now allows the release. Not done: the comparison of one pattern at block sizes 64, 512 and 2048 (no processor-level test written) and **your listening**: play a drone with a fast pitch or filter envelope and a short gate, stop and start the transport, and bypass the gater; listen for steps and clicks, and try a small and a large buffer in your host. mu-Tant goldens are stale and will move; the processor-level category (`mu-tant-tests mu-tant-processor`) passes, 0 failures of 18. In-testing release notes for all of this are on the site (`16922c96`).

### Extra · 2026-10-31 · JUCE MIDI abort
Every Debug Standalone aborted at startup on this PC: JUCE 9.0.4 crashes when `midiInOpen` fails (the MOTU port; likely after the 29 Oct Windows updates). Upstream JUCE issue 1769, fixed on `develop`, not in 9.0.4. A local guard is in `D:\JUCE` and logged in `docs/design-plugin-family.md` ("Local JUCE patches"), diff in `juce-patches/`. Only mu-Clid has been rebuilt with it; the others need a rebuild.

### Extra · 2026-10-31
`mu-core-tests` started at 4 failures; 3 were tests that did not expect the new gain ramps (master pan, return mute) and were fixed in `0af80996` by running one settle block. The remaining one (NaN counter) was a stale expectation: the insert stage now contains the NaN before the master probe (audit fix), so the test now expects the counter to stay 0; mu-core-tests is 0 failures of 836.
