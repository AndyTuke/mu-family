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
| `A1_master.muClid` | full preset | Kick + 64-step master loop (`mstrLoop=4`), rhythm 0 loops every 0.5s | A1: a full-preset swap must commit at the 8s **master** boundary, not rhythm 0's loop (the 32-vs-64 bug). |
| `A3_filter.muClid` | full preset | Steady hat + stepped LFO → `filter.cutoff` | A3: cutoff modulation sweeps the spectral centroid. |
| `A4_amp.muClid` | full preset | 16th-note hat + LFO → `amp.level` | A4: amplitude modulation (tremolo) swings the windowed level. |
| `A5_pitch.muClid` | full preset | Tonal synth + LFO → `pitch.semitones` | A5: pitch modulation (vibrato) moves the fundamental. |
| `A6_pattern.muClid` | full preset | Kick (base 2 hits) + LFO → `euclid.a.hits` | A6: pattern modulation raises hit density via the Stage-A/B recompute. |
| `A7_envelope.muClid` | full preset | Synth + LFO → `amp.decay` | A7: envelope-timing modulation lengthens the tails. (release is a no-op on one-shots, so decay carries this axis.) |
| `A8_tail.muClid` | full preset | Long-decay tonal synth on every beat | A8: a long voice rings out smoothly across a swap commit (deeper tail-out than TS_swap). |
| `A9_base.muClid` + `A9_hat.muRhythm` | full preset + single rhythm | Kick base (2s master loop) + hat staged onto slot 0 | A9: per-rhythm hot-swap (`stageRhythmPreset`) defers to the loop point then changes content (kick→hat). |

(A2 reuses `TS1`/`TS2` — it injects a MIDI program change rather than needing its own preset.)

The modulation presets (A3–A7) each pair an LFO `<Seq mode="Stepped">` with one
`<Asgn>` — see *Authoring modulation presets* in `../README.md` for the gotchas
(Stepped vs Smooth, `amp.release` no-op, calibrating against a no-mod control).

## Adding a preset

1. Drop the `.muRhythm` / `.muClid` here.
2. Reference it from an expectation JSON as `tests/presets/<name>` (the runner resolves
   repo-local paths as well as content-folder paths).
3. Document it in the table above + add the test row to `../../tests.md`.
