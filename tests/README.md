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
├── presets/                  -- repo-local test presets (e.g. TS1/TS2.muClid),
│                                versioned with the tests for reproducibility
├── expectations/             -- per-test JSON: render config + assertions
│   ├── T11.json
│   ├── T12.json
│   └── T13.json
├── scripts/
│   ├── analyse.py            -- WAV in, pass/fail out
│   └── run-listening-tests.py -- orchestrator: build product + render + analyse
└── _out/                     -- rendered WAVs (gitignored)
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

`--swap-preset`/`--swap-at` load a second preset partway through the render while
the sequencer is playing, exercising the real deferred / prestaged / tail-out
full-preset hot-swap path (the swap commits at the next loop point, not at the
flag time). See `tests/expectations/TS_swap.json` for a worked example.

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

## CI integration (future)

The pipeline exits with non-zero status when any assertion fails, so
`run-listening-tests.py` plugs into CI as a build step. Builds where the test
suite goes red should be blocked from auto-deploying to the OneDrive tester
folder.
