# Stage 35 — Robust preset format (rename + de-normalise + string algorithm names)

## Goal

Make `.muRhyth` and `.muclid` preset files immune to common forward-compatibility
breakages so new algorithms can be added without invalidating existing presets.

## Why now

#430 surfaced the underlying issue: presets store JUCE-normalised 0..1 values
that re-interpret against the *current* APVTS range on load. Three classes of
change to the audio engine currently break presets:

1. **Range widening** — `drvChar` 0..10 → 0..12 in #422/#423 silently remapped
   every old preset's `TapeSat` (10) to `Vocoder` (12), and the legacy `drvDrv`
   value got read as Vocoder waveshape → Pink-Noise carrier → noise on every hit.
2. **Skew changes** — log/linear skew shifts where in 0..1 a particular actual
   value lives. Any future skew tweak corrupts saved cutoff knobs etc.
3. **Algorithm reordering** — dropdown order is currently hard-coded by index.
   Reordering or inserting in the middle silently scrambles every preset.

The #430 fix is a band-aid (one-time normalised-value rescale). Stage 35 is the
permanent fix: presets store actual numbers + string algorithm names, so the
audio code can evolve without touching the preset format.

## Naming change

`drv*` IDs predate the insert-processor refactor that added EQ / Comp / Limiter /
RingMod / TapeSat / Karplus / Vocoder — they're no longer just "drive" algorithms.
Rename all `drv*` to `ins*` for clarity:

| Old APVTS ID | New APVTS ID | New VoiceParams field |
|---|---|---|
| `drvChar`    | `insChar`    | `insertAlgo`     |
| `drvDrv`     | `insDrv`     | `insertDrive`    |
| `drvOut`     | `insOut`     | `insertOutput`   |
| `drvBits`    | `insBits`    | `insertBits`     |
| `drvRate`    | `insRate`    | `insertRate`     |
| `drvDit`     | `insDit`     | `insertDither`   |
| `drvTon`     | `insTon`     | `insertTone`     |
| `eqMidGain`  | `insEqMid`   | `insertEqMid`    |

Master-insert IDs (`mst_ins*`) already use the `ins` prefix — leave as-is.

## Format change

`.muRhyth` and `.muclid` files migrate to **version 2** with these rules:

### 1. Algorithm selectors → string names

Per-param lookup tables map name ↔ index. Saved as the string name:

```cpp
// In PluginProcessor_Internal.h or a new AlgorithmNames.h
inline const char* const kInsertAlgorithmNames[] = {
    "None", "SoftClip", "HardClip", "Fold", "Bitcrusher", "Clipper",
    "EQ", "Compressor", "Limiter", "RingMod", "TapeSat",
    "Karplus", "Vocoder", nullptr
};
inline const char* const kFilterTypeNames[] = {
    "LP12", "HP12", "BP12", "Notch12", "LP24", "HP24", "BP24", "LP6",
    "Comb+", "AP12", "Notch24", "HP6", "Peak", "LowShelf", "HighShelf", "Comb-",
    nullptr
};
// + kEffectAlgorithmNames + kReverbAlgorithmNames from FXAlgorithmRegistry
```

The same tables drive the UI dropdowns (already do, via `FXAlgorithmRegistry`
for effects/reverb; need to add for insert/filter), so name strings stay in
lockstep with what the user sees.

**Save:** `r0_insChar="Bitcrusher"` (not `"4"`)
**Load:** linear-scan the name table → index → `apvts.setValueNotifyingHost(convertTo0to1(index))`

### 2. Continuous and integer params → actual values

```cpp
r0_stepsA="16"          (not "0.04...")
r0_hitsA="4"            (not "0.0625")
r0_fltCut="8000.0"      (not "0.195...")
r0_aEnvAtk="0.005"      (not "0.45...")  -- seconds directly
```

Save: `apvts.getRawParameterValue(id)->load()`
Load: `apvts.getParameter(id)->setValueNotifyingHost(param->convertTo0to1(actual))`

### 3. Boolean params → "true" / "false"

Already de-normalised effectively (stored 0 or 1). Switch to string for
readability:

```cpp
r0_patLeg="true"        (not "1.0")
r0_aEnvLeg="false"      (not "0.0")
```

## Migration matrix on load

`presetVersion` property drives the branch:

| `presetVersion` | Load path |
|---|---|
| absent or `0` | #430 legacy: read normalised, apply legacy migration (drvChar 0..10→0..12, drvBits 1..16→0..16), call `setValueNotifyingHost(migratedNorm)` |
| `1` | Read normalised, call `setValueNotifyingHost(norm)` (no shift needed) |
| `2` (current) | Read actual numbers + algorithm name strings, look up name → index, call `setValueNotifyingHost(convertTo0to1(actual))` |

This keeps #430's compat layer working — Andy's existing presets still load
correctly. New saves always write v2.

## Lookup-table location

Keep names with their algorithm tables:

- **Effects + Reverb**: `FXAlgorithmRegistry` already has a name string per
  algorithm. Use that directly.
- **Inserts**: add `kAlgoName` static field to each `InsertAlgorithmBase`
  subclass, or maintain a parallel `kInsertAlgorithmNames` table in
  `InsertProcessor.h` next to the dispatch table ctor. Single source of truth.
- **Filters**: maintain `kFilterTypeNames` in `MultiModeFilter.h` next to the
  algorithm array. The UI dropdown choice list should read from this table
  rather than hard-coding the names a second time.

The save/load path imports these constants. UI dropdowns read from them too —
single source of truth for "what's this algorithm called."

## What stays normalised

Nothing in user-facing preset files. Host-state path (DAW project, `setStateInformation`)
keeps using JUCE's own `apvts.copyState()` which stores de-normalised already —
no change needed there.

## What this protects against

| Engine change | v0/v1 presets | v2 presets |
|---|---|---|
| Widen drvChar 0..10 → 0..12 | ❌ shifts indices | ✅ name lookup unchanged |
| Add a 14th insert algorithm at the end | ❌ shifts indices | ✅ name not in old table — clamp to None on load |
| Rename "Bitcrusher" → "BitCrush" | n/a | ⚠ requires migration table (one-time alias) |
| Reorder filter list | ❌ shifts indices | ✅ name lookup unchanged |
| Insert RingMod between SoftClip and HardClip | ❌ shifts everything | ✅ all names resolve correctly |
| Remove "Limiter" | ❌ shifts everything after | ✅ name not found → fall back to None with a log entry |
| Change `fltCut` skew curve | ❌ values land at wrong frequencies | ✅ actual Hz value preserved |
| Widen `fltCut` to 10..30000 | ❌ shifts | ✅ actual value preserved (clamps if out of new range) |

## Sequencing (step-by-step with test gates)

### Step 1 — Lookup tables + name conversion helpers

Add `kInsertAlgorithmNames`, `kFilterTypeNames`, and helpers
`indexFromName(table, name)` / `nameFromIndex(table, idx)`. No callers yet.

**Test:** unit test or just `static_assert` count matches `kNumAlgorithms`.

### Step 2 — Rename `drv*` → `ins*` in APVTS IDs + VoiceParams

Mechanical find-replace across these files:
- `Source/Sequencer/Rhythm.h` — `VoiceParams` member names
- `Source/Audio/VoiceEngine.cpp`/.h — reads of `activeParams.drive*`
- `Source/Audio/InsertProcessor.cpp`/.h — algorithm classes reading `p.drive*`
- All `Source/Audio/Processing/InsertFX/*.h` — `p.drive*` reads
- `Source/PluginProcessor_Internal.h` — `kRhythmSuffixes` table + `applyRhythmSuffix`
- `Source/PluginProcessor_APVTS.cpp` — `createParameterLayout` + `pushRhythmToAPVTS`
- `Source/PluginProcessor.cpp` — modulation pass write-back
- `Source/UI/VoiceSection.cpp`/.h — `refreshSuffix` calls, knob bindings
- `Source/UI/MixerChannel_Insert.cpp` — `configureInsertAlgorithm` cases

Migration: in `migrateLegacyHostState`, copy old `drv*` PARAM children to new
`ins*` IDs in the ValueTree before `apvts.replaceState`. This keeps DAW projects
saved before Stage 35 loading correctly (Andy is pre-release so this is more
defensive than necessary, but it's cheap).

**Test:** rebuild clean. Load an old v0/v1 preset via #430 path — drvChar values
should resolve to the right algorithm under the new `insChar` ID.

### Step 3 — De-normalise preset save format

Update `saveRhythmPreset`, `saveRhythmPresetToFile`, `savePreset` to:
- Write `presetVersion="2"`.
- For algorithm-selector params: write the name string via the lookup table.
- For all other params: write the actual value via `apvts.getRawParameterValue(id)->load()`.
- For boolean params: write `"true"` / `"false"`.

### Step 4 — Add v2 load path

In `applyRhythmPreset`, `stageRhythmPreset`, `loadPreset`, branch on
`presetVersion`:
- `>= 2`: read actual values + algorithm names → look up index → `setValueNotifyingHost(convertTo0to1(actual))`.
- Keep existing v0/v1 paths intact.

For algorithm names: if the name isn't in the current table, set the param to
0 (None / first algorithm) and call `onLoadError` with a "Preset uses unknown
algorithm 'X' — falling back to None" message. UI shows the toast.

**Test 4.1:** Save a preset on the new build. Reopen. All values should match.
The XML should be human-readable.

**Test 4.2:** Load Andy's existing v0/v1 presets. Should still load identically
to today (via the #430 path).

**Test 4.3:** Hand-edit a v2 preset XML — change `r0_insChar="Bitcrusher"` to
`r0_insChar="Vocoder"`, reload. Should switch algorithm.

**Test 4.4:** Hand-edit to an unknown name like `"FutureAlgo"`. Reload should
fall back to None with the user-visible error.

### Step 5 — Documentation

Update `docs/design-presets.md` with:
- The new preset XML schema (v2)
- The lookup-table convention for algorithm selectors
- The append-only invariant for algorithm IDs (don't reorder; only add at end)
- The migration ladder (v0 → v1 via #430; v0/v1 → v2 read-only compat)

## Out of scope for Stage 35

- Migrating mixer-channel insert IDs (`ch*_*`) — same naming applies but no
  range issues yet; do alongside any future channel-insert addition.
- Per-modulator name strings (ControlSequence IDs are already strings, fine).
- DAW automation lane reorder safety — that's a JUCE-level problem and can't
  be solved without changing the parameter index, which is set in `createParameterLayout`.
  Document the constraint instead: algorithm-selector params are ordered
  append-only; reordering means breaking automation in existing DAW projects.

## Risks and rollback

- **Step 2** (rename) is the biggest change by file count. Risk: missed reference
  in some file that no test catches. Mitigation: grep for the old IDs / member
  names after each TU is updated. Rollback is `git revert`.
- **Step 3** save format change. Risk: malformed XML if a name lookup misses.
  Mitigation: assert on save that every algorithm index has a name in the table
  — fail loudly at save time rather than write a half-broken file.
- **Step 4** v2 load path. Risk: regressed v0/v1 load. Mitigation: test
  cases above; keep the existing #430 code path intact rather than rewriting it.

## Backlog entries to log

One per step: 431 (lookup tables), 432 (rename), 433 (save de-normalised),
434 (v2 load path), 435 (docs). Numbering starts at next free issue.
