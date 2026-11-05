# Build PC → laptop

Written only by the build PC. The laptop reads it and answers in [laptop-to-build.md](laptop-to-build.md). Protocol: [README.md](README.md).

Next id: B-006

(B-001 resolved and removed: see the Extra entry below.)

## Status (build PC)

- Written: 2026-11-05, end of the mu-Tilate session. Build number 1457 (`mu-core/BuildNumber.h`, focused Debug builds, no increment). Everything is committed and pushed (`5f57b642` plus the handover and backlog commits after it).
- mu-Tilate: `mu-tilate-tests` 110 results, 0 failures (Debug). New this session and not yet seen or heard by Andy: rows #1759 to #1765 (see B-005). JUCE is now 9.0.5 (`D:\JUCE`, pin bumped, the local MIDI patch dropped); only mu-Tilate and its tests were rebuilt on it. No Release build, no pluginval run.
- Other products: not rebuilt on JUCE 9.0.5 yet; their golden and pin failures are the stale ones in B-002, #1751 (mu-Tronic Pattern hash) is open.
- `D:\JUCE-dev` is a scratch clone of JUCE `develop` for upstream work (L-012 is answered below).

## Open

### B-005 · 2026-11-05 · mu-Tilate: look and listen to this session's changes, and measure one thing
Rows: #1759 to #1765 (and #1745) · Commits: `c25d298f`, `7dfe426f`, `bb168712`, `ef4f5fdb`, `cc03ef84`, `26e4726b`, `9d13a47e`, `ff8ae934` · Needs: look | listen | measure
Do: run mu-Tilate (Debug Standalone). Look: SAMPLE panel in the top row, SEQUENCER and TIMELINE VIEW panels, the walking-man follow toggle (zoom the timeline, play, switch it off), hints in the status bar only (no popups, in every product: mu-core ShellChrome), the fade corners and the bend handles on a hit. Listen: the audition starts at once (play button, tempo and slice), the sample browser (Load loop) auditions a selected file, sample play does nothing while the transport plays, the missing first hit (#1758 fix, and the loop-wrap downbeat fix in #1765). Measure (#1765 R1): the worst-case time of a stretcher trigger in the audio callback (Strength 0, Default window, 88.2 kHz).
Report: anything wrong on #1745 or as a new row; the R1 number on #1765. Not done on this PC: nothing was listened to, the browser audition was not heard, the hint change was looked at in mu-Tilate only, no Release build.

### B-004 · 2026-11-03 · mu-Tilate: look and listen through everything built since 1745
Rows: #1745 (the list), #1748 to #1755 · Commits: up to `7ec11c54` · Needs: look | listen
Do: run mu-Tilate (Debug Standalone) and go through the #1745 check list: range bar, timeline edge trim/retime, drop onto a longer block, marker snap, double-click add, hover-cross delete, rename by double-click, scroll bar, loop toggle, detector on the Sequencer page, the play-area handles and Crop (dimmed "cropped" hits), moving the area while a play button sounds, the playhead, the one-row top panel with the icon play buttons, the Layer Sound strip on the Sequencer view, dragging the top of a hit to set its volume, the new modulation targets (incl. Mixer Pan and the three sends), Sync on both tape inserts.
Report: anything that looks or sounds wrong, on #1745 or as a new row. Also: which play button has a start delay and how long the loop is (in the tests every button starts in under 20 ms); #1720 (Tape Spring name and extension bank) and #1751 (re-record the mu-Tronic hash?) need a decision.
Not done on this PC: nothing listened, no Release build, the other products' mod-target tables have not adopted the coverage check.

### B-003 · 2026-11-01 · mu-Tilate: decisions and reviews that need planning, not a build
Rows: #1723, #1725, #1726, #1728, #1730, #1720 · Commits: up to d8796a0e and bb666382 · Needs: decide | review
Do: (1) #1725: decide what Length (bars) and Snap Markers should be (a real timeline-length parameter with a UI control, or removed); the timeline length lives in the saved state today. (2) #1726: a ux-controller ruling on tokenising the mu-Tilate layout sizes (SlicePanel, PhraseContent, PhraseTimeline, LoopWaveform) into MuLookAndFeel. (3) #1723: pick royalty-free (redistributable in a commercial plugin) sources for factory loops, and plan the manual and release wiring. (4) #1728: whether the percentile normalisation and a grid-aware tempo check are worth doing, and who recalibrates the default sensitivity. (5) #1730: Andy edits the stale lines of `mu-tilate/CLAUDE.md` (7, 13, 17, 28, 34, 49-56) and the root `CLAUDE.md` layout line ("planned, design only, scaffold only"): I did not edit CLAUDE.md files on an agent's say-so. (6) #1720: research the RE-201 spring reverb and the existing tape echo against it (design doc only).
Report: decisions in the rows or the design docs; nothing for me to build until they are made. Not done on this PC: listening to the mu-Tilate demos in `build/mutilate-demo` (plain, pre-fade, stretch), Release build, pluginval, CPU measurement of the stretcher prime at 192 kHz.

### B-002 · 2026-10-31 · Stale goldens, mu-Toni pins (the manifests and corpus are done)
Rows: #1719 · Needs: decide
Done: the frozen manifests and compat-corpus param dumps of mu-On, mu-Clid, mu-Tant and mu-Toni were regenerated and reviewed (`f5087e89`): the only changes are the insert algorithm range 0..14 to 0..15 (the Tape Delay insert, `829c2993`) and the re-save hashes that follow it. Nothing else changed in those files.
Still stale, left that way on purpose (Andy's rule: no golden re-records while sounds change): the golden renders (mu-On 9, mu-Clid 8+, mu-Tant 11, mu-Toni 12) and three mu-Toni behaviour pins (`readVoice: a modulation assignment moves the published value`, cutoff 9499 / 3322 / 13520 Hz recorded 2026-10-15, 11890 / 3618 / 17832 now, after the 2026-10-20 to 22 modulation work). Say when the sound work has settled and I will re-record them all at once.

## Results

### Re L-012 · 2026-11-05 · Done (patch verified, with one change to the test setup)
Clone: `D:\JUCE-dev` (JUCE `develop`, a reusable scratch clone; patch on branch `vst3-wide-class-name-utf8`, applied with `git am --keep-cr`, author is the noreply address); work files in `D:\JUCE-dev-work`. Step 2 (`run-widen-test.ps1`): "All widenNonAsciiUtf8 tests passed", no `/W4` warnings; it needed `/utf-8` added to its `cl` line (the test source has non-ASCII literals; fixed in the script). Step 3 first run, as the script was: unpatched AND patched both gave `?-Test` (003F 002D 0054 0065 0073 0074): without `/utf-8` MSVC turns the mu into `?` (warning C4566) before JUCE sees it, so the patch has nothing to convert. Second run with `/utf-8` (new `-Utf8` switch in `check-real-build.ps1`, sets `CXXFLAGS`): unpatched FAIL, `FFCE FFBC 002D 0054 0065 0073 0074` (the two UTF-8 bytes each widened to a char); patched PASS, `03BC 002D 0054 0065 0073 0074`, both classes. So the bug reproduces and the patch fixes it, for a project compiled with `/utf-8`. Step 4, Bitwig (Andy, 2026-11-05, the `proj-patched-utf8` build): the plugin window title reads `T2 / Audio 4 / μ-Test` and the plug-in browser lists `μ-Test` (three entries under the vendor `yourcompany`: the same patched bundle found in the scanned folder and the copies Andy made); the device strip's vertical label shows `M-TEST`, which is Bitwig upper-casing the name (capital Greek mu looks like M), not a garbled name. The plugin loads and shows its UI. ASCII check (new `-Ascii` switch in `check-real-build.ps1`, plugin named `Ascii-Test`, patched branch, built without `/utf-8`): PASS for both classes, code points `0041 0073 0063 0069 0069 002D 0054 0065 0073 0074`, so a plain ASCII name is unaffected. Not done: a host comparison with the unpatched build, and any non-MSVC compiler or non-Windows platform.
Environment for the PR: MSVC `cl` 19.51.36260 for x64 (Visual Studio 2026, "Visual Studio 18 2026" CMake generator, toolset 14.51.36231), CMake 4.3.1, Windows 11 Pro 10.0.26300, Bitwig Studio (version not noted), JUCE `develop` at `ec7bc80d2e` (unpatched) and the patch commit `d599d637cd` on top of it, Release configuration, the `examples/CMake/AudioPlugin` example built as VST3 only. For the PR: say that the fix applies to projects built with `/utf-8` (does JUCE's CMake add it for a non-ASCII PLUGIN_NAME? In this run it did not, which may be worth raising upstream). Scripts changed: `handover/juce-1774/run-widen-test.ps1`, `check-real-build.ps1` (`-Utf8` and `-Ascii` switches).

### Extra · 2026-10-31 · JUCE MIDI abort
Every Debug Standalone aborted at startup on this PC: JUCE 9.0.4 crashes when `midiInOpen` fails (the MOTU port; likely after the 29 Oct Windows updates). Upstream JUCE issue 1769, fixed on `develop`, not in 9.0.4. A local guard is in `D:\JUCE` and logged in `docs/design-plugin-family.md` ("Local JUCE patches"), diff in `juce-patches/`. Only mu-Clid has been rebuilt with it; the others need a rebuild.

### Extra · 2026-10-31
`mu-core-tests` started at 4 failures; 3 were tests that did not expect the new gain ramps (master pan, return mute) and were fixed in `0af80996` by running one settle block. The remaining one (NaN counter) was a stale expectation: the insert stage now contains the NaN before the master probe (audit fix), so the test now expects the counter to stay 0; mu-core-tests is 0 failures of 836.
