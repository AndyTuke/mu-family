# Listening-test pipeline

Automates the "render this preset, measure the audio, compare to expectations"
loop. Lets us catch regressions in audio behaviour (e.g. "Karplus tail still
present 100ms past env idle") without a human ear in the loop.

Designed to be reusable across the **mu-family** of plugins (mu-clid, mu-tant,
mu-toni …). The render binary is per-plugin; the analysis script and the
expectations JSON schema are plugin-agnostic.

## Layout

```
tests/
├── presets/                  -- repo-local test presets (TS1/TS2, A1/A3-A9 …),
│                                versioned with the tests for reproducibility
├── expectations/             -- per-test JSON: render config + assertions
│   ├── T11.json … TS_swap.json   -- original listening tests
│   └── A1.json … A9.json         -- swap-boundary + modulation suite
├── scripts/
│   ├── analyse.py            -- WAV in, pass/fail out
│   ├── run-listening-tests.py -- orchestrator: build product + render + analyse
│   └── check-build-artifacts.py -- C6: µ-name + build-number guard (no audio)
└── _out/                     -- rendered WAVs + scratch (gitignored)
```

The render-mode plumbing lives in [Source/Plugin/RenderMode.{h,cpp}](../Source/Plugin/RenderMode.cpp);
test presets themselves live in the content folder (`$MUCLID_CONTENT_DIR/Rhythms/T*.muRhythm`)
alongside user-facing presets, so they appear in the GUI's preset dropdown
under the `test` category and can be opened by hand for ad-hoc listening too.

## Running

```bash
# Make sure the standalone is built (Debug is faster to iterate, Release is what testers see)
cmake --build build --config Debug

# Run the full suite
python tests/scripts/run-listening-tests.py --config Debug

# Run a single test
python tests/scripts/run-listening-tests.py --config Debug --filter T12

# Manual single-step: render + analyse separately
build/mu-clid_artefacts/Debug/Standalone/<plugin>.exe \
    --render --out tests/_out/T12.wav \
    --preset "$MUCLID_CONTENT_DIR/Rhythms/T12.muRhythm" \
    --seconds 1.5
python tests/scripts/analyse.py tests/_out/T12.wav tests/expectations/T12.json
```

Set `MUCLID_CONTENT_DIR` if your content folder isn't the default
(`D:\OneDrive\Documents\TDP\muClid`).

## Render mode

`<standalone>.exe --render --out <wav> [flags]` runs headless: no GUI, no
audio device. Skips the `_default.muClid` auto-load so every test starts
from a fresh single-rhythm default. Flags:

| Flag            | Default | Description                                       |
|-----------------|---------|---------------------------------------------------|
| `--out`         | (req'd) | Output WAV path                                   |
| `--preset`      | none    | `.muRhythm` (single-rhythm) or `.muClid` (session) |
| `--seconds`     | 4.0     | Render duration                                   |
| `--samplerate`  | 48000   | Render sample rate                                |
| `--blocksize`   | 512     | Process block size                                |
| `--swap-preset` | none    | A 2nd preset loaded mid-render (full-preset hot-swap test) |
| `--swap-at`     | none    | When (seconds) to load `--swap-preset`; needs both flags |
| `--swap-rhythm-preset` | none | A `.muRhythm` staged onto one slot mid-render (per-rhythm hot-swap) |
| `--swap-rhythm-slot`   | 0    | Which slot `--swap-rhythm-preset` targets |
| `--swap-rhythm-at`     | none | When (seconds) to stage the per-rhythm swap; needs preset + at |
| `--midi-program`        | none | Program number to inject as a channel-9 PC (seeds the full-preset map) |
| `--midi-program-preset` | none | Preset the injected program maps to |
| `--midi-program-at`     | none | When (seconds) to inject the program change; needs all three |

`--swap-preset`/`--swap-at` load a second preset partway through the render while
the sequencer is playing, exercising the real deferred / prestaged / tail-out
full-preset hot-swap path (the swap commits at the next loop point, not at the
flag time). See `tests/expectations/TS_swap.json` (free-running) and `A1.json`
(master-loop boundary) for worked examples.

`--swap-rhythm-*` stages a single `.muRhythm` onto one slot via the per-rhythm
hot-swap path (`stageRhythmPreset`), distinct from the full-preset load — see
`A9.json`. `--midi-program-*` seeds the channel-9 full-preset map then injects a
MIDI program change mid-render, exercising the MIDI-PC → preset trigger path end
to end — see `A2.json`.

Output is 24-bit stereo WAV, written via `juce::WavAudioFormat`.

## Auto-load in GUI mode

On normal GUI launch (no `--render`), the standalone auto-loads the first of:

1. `<content>/Presets/_default.muClid` -- a full session.
2. `<content>/Rhythms/_default.muRhythm` -- a single rhythm applied to slot 0.

Dropping a `_default.muRhythm` into the content folder lets you iterate on a
test case without clicking through the preset browser.

## Expectations JSON

```json
{
  "test": "T12",
  "description": "Karplus rings past env idle.",
  "render": {
    "preset": "Rhythms/T12.muRhythm",   // resolved under $MUCLID_CONTENT_DIR, else repo root
    "seconds": 1.5,
    "sample_rate": 48000,
    "block_size": 512
    // optional full-preset swap test:
    //   "swap_preset": "tests/presets/TS2.muClid",  // repo-local preset
    //   "swap_at": 2.0                               // seconds; loaded mid-render
  },
  "assertions": [
    {
      "name": "first_hit_audible",
      "metric": "rms_dbfs",
      "channel": "any",
      "t_window": [0.05, 0.15],
      "min": -30
    }
  ]
}
```

### Channels

- `"any"` -- channel with the higher peak (default; tolerates mono-on-one-side renders)
- `"left"` / `"right"`
- `"mono"` -- average of all channels

### Metrics

| metric                        | params                                                                | meaning                                                                  |
|-------------------------------|-----------------------------------------------------------------------|--------------------------------------------------------------------------|
| `peak_dbfs`                   | `t_window`, `min`, `max`                                              | Peak amplitude in dBFS within the window                                 |
| `rms_dbfs`                    | `t_window`, `min`, `max`                                              | RMS amplitude in dBFS within the window                                  |
| `duration_above_threshold`    | `t_window`, `threshold_dbfs`, `window_ms`, `min_seconds`, `max_seconds`| Seconds the per-`window_ms` RMS stays above `threshold_dbfs`             |
| `spectral_peak_near`          | `t_window`, `frequency_hz`, `tolerance_hz`, `search_hz=[lo, hi]`      | Strongest spectral peak inside `search_hz` must be within tolerance of target frequency |
| `spectral_centroid`           | `t_window`, `search_hz`, `min_hz`, `max_hz`                          | Energy-weighted mean frequency (brightness) of the window — e.g. "this is a kick (low)" vs "a hat (high)" |
| `spectral_centroid_range`     | `t_window`, `search_hz`, `window_ms`, `min_hz`, `max_hz`             | max−min of the per-sub-window centroid → a filter-cutoff sweep is active (A3) |
| `rms_range_db`                | `t_window`, `window_ms`, `floor_dbfs`, `min_db`, `max_db`            | max−min of per-sub-window RMS (dB), ignoring sub-windows below `floor_dbfs` → level/tremolo modulation (A4). Use `window_ms` wider than the hit spacing so per-hit dynamics average out |
| `spectral_peak_range`         | `t_window`, `search_hz`, `window_ms`, `min_hz`, `max_hz`             | max−min of the per-sub-window strongest peak → the fundamental is moving (pitch/vibrato, A5) |
| `onset_count`                 | `t_window`, `window_ms`, `threshold_dbfs`, `refractory_ms`, `min`, `max` | Count of rising-edge onsets (RMS crossing the threshold from below) → hit density, e.g. pattern modulation (A6) |

The five modulation metrics measure *change over time* within a window — the
signature of "modulation is working." Each modulation test is calibrated against
a no-modulation control (delete the `<Asgn>`) so the threshold sits clear of the
un-modulated baseline.

All assertions accept `"comment"` -- shown alongside the result when verbose or
when the assertion fails.

## Adding a new listening test

1. **Pick a number.** `T<N>` where `N` is the next free index in the existing
   listening-test sequence (T11, T12, T13 ... -- continue T14 etc.).
2. **Write the preset** at `$MUCLID_CONTENT_DIR/Rhythms/T<N>.muRhythm`. Use
   `presetCategory="test"` so it groups in the dropdown.
3. **Render it once manually** to see what the audio looks like:
   ```bash
   build/.../Standalone/<plugin>.exe --render --out tests/_out/T<N>.wav \
       --preset "$MUCLID_CONTENT_DIR/Rhythms/T<N>.muRhythm" --seconds <s>
   ```
4. **Probe RMS values** to figure out sensible thresholds:
   ```python
   from tests.scripts.analyse import load_wav, rms, dbfs
   a = load_wav(Path('tests/_out/T<N>.wav'))
   for t in range(0, 200, 5):
       win = a.channel('any')[int(t*a.rate/100):int((t+5)*a.rate/100)]
       print(f'{t/100:.2f}: {dbfs(rms(win)):.2f} dBFS')
   ```
5. **Write `tests/expectations/T<N>.json`** with the render config + assertions
   tied to the measurements you observed.
6. **Verify via the orchestrator**:
   ```bash
   python tests/scripts/run-listening-tests.py --filter T<N>
   ```
7. **Add a row to `backlog.md`** under the `🔵 Tests` section so the
   regression is documented + addressable when it breaks.

## Test catalogue

**Audio / listening (`A`-series + `T`-series), run via `run-listening-tests.py`:**

| Test | What it proves |
|------|----------------|
| T11–T13 | Original regressions: filter ring, Karplus tail, 8-hit overlap |
| TS_swap | Full-preset hot-swap, free-running: defer + tail-out + silence |
| A1   | Full-preset swap defers to the **master** loop boundary, not a rhythm's loop (the "Well this is nice" 32-vs-64 bug) |
| A2   | MIDI program-change (ch 9) → deferred full-preset load |
| A3   | Modulation → `filter.cutoff` (centroid sweep) |
| A4   | Modulation → `amp.level` (tremolo) |
| A5   | Modulation → `pitch.semitones` (vibrato) |
| A6   | Modulation → `euclid.a.hits` (pattern recompute, hit density) |
| A7   | Modulation → amp envelope timing (`amp.decay`) |
| A8   | Deep tail-out: a long tonal voice rings out smoothly across a swap |
| A9   | Per-rhythm hot-swap (`stageRhythmPreset`): defer + content change |

**Compile / unit (`C`-series), run via the `mu-clid-tests` console app** — see
*Compile tests* below:

| Test | What it proves |
|------|----------------|
| C1 | `migrateInsertSlotsV3`: v2 9-field insert → 4 normalised slots, per algo |
| C2 | `migrateLegacyHostState`: pre-#217 ADSR 0..100 → 0..10 s + End sentinel |
| C3 | HotSwap loop-boundary predicates, incl. the #653 free-running fallback |
| C4 | `MidiFullPresetMap` (ch-9) JSON save→reload round-trip |
| C5 | Proportion-space modulation skew forward/inverse round-trips |
| C6 | Built binaries carry UTF-8 `µ-Clid` (not `?-Clid`) + build-number match |

## Authoring modulation presets (A3–A7)

A modulation test is a tiny preset with one `<Seq>` (an LFO / control sequence)
and one `<Asgn>` routing it to a destination, plus a metric that detects the
resulting change over time. Gotchas learned building these:

- **`<Seq mode="Stepped">` consumes `<Step v="…"/>` values; `mode="Smooth"`
  interpolates `<Point>` Bézier nodes.** Match the mode to the data you author.
  A mode/data mismatch (e.g. `Smooth` with only `<Step>`s) used to load silently
  inert; it now **self-heals** — the loader flips the mode to match the data
  present, and `ControlSequence::evaluate()` falls back to whichever array has
  data (belt + braces). Still: author it right so the saved mode reflects intent.
- **`amp.release` is a no-op for one-shot step triggers** (there is no note-off
  to start the release phase). To modulate the amp envelope's audible tail, drive
  `amp.decay` instead (see A7).
- **Always calibrate against a no-modulation control**: render the preset with
  the `<Asgn>` removed, measure the same metric, and set the threshold between
  the two. A modulated/un-modulated separation of <2× is too fragile.
- For a clean tremolo/amplitude metric, hit a steady stream (e.g. a hat on every
  16th) and measure `rms_range_db` with `window_ms` wider than the hit spacing so
  per-hit dynamics average out and only the slow LFO shows.

## Compile tests (C-series)

Deterministic unit tests (JUCE `UnitTest`) over pure logic — migrations, swap
boundaries, MIDI maps, skew math. They live in
[Source/Tests/](../Source/Tests/) and link into the `mu-clid-tests` console app:

```bash
cmake --build build --config Debug --target mu-clid-tests
build/mu-clid/mu-clid-tests_artefacts/Debug/mu-clid-tests.exe
```

The exe prints `mu-clid-tests: N failure(s) across M test result(s)` and exits
non-zero on any failure.

**C6** is a separate post-build script (no audio, no test exe):

```bash
python tests/scripts/check-build-artifacts.py --config Release
```

It scans the built VST3/CLAP/Standalone binaries (mu-clid + Lite) for the UTF-8
`µ-Clid` display name + absence of the mangled `?-Clid` (#654), and checks
`build_number.txt` against `BuildNumber.h`. Run it after a Release build before
trusting the OneDrive deploy.

## CI integration (future)

The pipeline exits with non-zero status when any assertion fails, so
`run-listening-tests.py` plugs into CI as a build step. Builds where the test
suite goes red should be blocked from auto-deploying to the OneDrive tester
folder.
