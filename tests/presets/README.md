# Test presets

Repo-local presets used by the listening-test suite (`tests/scripts/run-listening-tests.py`).
Kept here — not in the content folder — so they're versioned with the tests and a
checkout reproduces every render exactly. Each is paired with an expectation file in
`../expectations/<name>.json` (assertions) and a row in `../../tests.md` (human description).

Samples referenced by relative name (e.g. `Kick 01.wav`) resolve against the content
folder's `Samples/` dir at render time.

| Preset | Type | What it is | What it tests |
|---|---|---|---|
| `T11.muRhythm` | single rhythm | Strong ladder LP resonance + long sustained sample | Filter resonance fades with the amp-env release (env-gate sits *after* the filter), not cut at `markRetired`. (#627) |
| `T12.muRhythm` | single rhythm | Karplus insert, high feedback, short decay | Insert-FX tails (Karplus / comb) ring past the sample end and fade with the env release on the active engine. (#627) |
| `T13.muRhythm` | single rhythm | Percussive pattern, overlapping retriggers | Engine-level amp-env retrigger behaviour — rapid hits share env state cleanly; pattern-legato pad sustain. (#627) |
| `TS1.muClid` | full preset | Single steady kick rhythm (low band) | Source preset for the full-preset hot-swap test (`TS_swap`). |
| `TS2.muClid` | full preset | **Empty** — one rhythm, zero hits, no sample | Swap target for `TS_swap`: makes no sound, so after the loop-point commit any audio is purely TS1's retired voices tailing out — isolating the swap-commit timing + tail-out. |

## Adding a preset

1. Drop the `.muRhythm` / `.muClid` here.
2. Reference it from an expectation JSON as `tests/presets/<name>` (the runner resolves
   repo-local paths as well as content-folder paths).
3. Document it in the table above + add the test row to `../../tests.md`.
