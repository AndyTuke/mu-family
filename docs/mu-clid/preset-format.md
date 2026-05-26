# μ-Clid preset format

This document describes the on-disk format for the two user-facing preset
file types and the in-host state. Updated for Stage 35 (v2 format).

## File types

| Extension       | Tree root        | Contents                                                 |
|-----------------|------------------|----------------------------------------------------------|
| `.muRhyth`      | `MuClidRhythm`   | One rhythm — sequencer + voice chain + modulators        |
| `.muclid`       | `MuClidPreset`   | Full session — up to 8 rhythms + global FX + mixer state |
| (DAW state)     | APVTS root tree  | Same APVTS state as `.muclid` but lives in the DAW project — written via JUCE's `setStateInformation`. Uses de-normalised PARAM children, not the preset XML schema. |

Both `.muRhyth` and `.muclid` are XML (UTF-8, no BOM) produced by
`juce::ValueTree::toXmlString()`. They are human-readable and intended to be
hand-editable for power users.

## Versioning

Both file types carry a `presetVersion` integer attribute on the root tree.
The current and **only** supported version is `2` — actual de-normalised
values + stable algorithm name strings.

Legacy versions (v0 / v1 — normalised values, pre-Stage-35) are **refused at
the load entry point** by `requireSupportedPresetVersion` with an `onLoadError`
message pointing the user at hand-conversion via the dev. The on-disk format
is simple enough that converting an old preset by hand is a few minutes; the
maintenance cost of carrying a multi-version compat ladder was higher than
the user value, so the v0 / v1 reader branches and their migration helpers
were removed.

The `presetVersion` attribute stays in saved files so future format changes
have a clean way to reject older files (or, if we keep more presets at that
point, to add a real migration ladder).

## `MuClidRhythm` schema

```xml
<MuClidRhythm presetName="Buzzin'" presetCategory="Pulses" presetVersion="2"
              presetDescription="..." presetEmbedSamples="0"
              r0_name="Buzzin'" r0_colour="1" r0_sample="C:/.../kick.wav"
              r0_stepsA="16"  r0_hitsA="4"  r0_rotA="0"  ...
              r0_drvChar="Bitcrusher"  r0_drvDrv="35.0"  r0_drvBits="6"  ...
              r0_aEnvLeg="true"  r0_pEnvLeg="false"  r0_patLeg="false"  ...
              r0_fltType="LP24"  r0_fltCut="234.7"  r0_fltRes="0.55"  ...>
  <Modulators>
    <Seq id="cs0" mode="0" polarity="1" loopNV="2" .../>
    <Asgn id="..." src="cs0_output" dest="filter.cutoff" depth="50" curve="0"/>
  </Modulators>
</MuClidRhythm>
```

### Per-rhythm parameter property naming

All rhythm parameter properties carry the `r0_` prefix. The "0" is purely
historical (a single rhythm preset isn't indexed); inside `.muclid` files,
the same suffixes appear *without* a `r0_` prefix because they're already
nested under a `Rhythm` child.

The list of suffixes is the canonical [`mu_pp::kRhythmParamDefs[]`](../Source/RhythmParamTable.h)
table. The table is the single source of truth — adding a new per-rhythm
parameter means adding one entry there + one APVTS-layout entry in
[`createParameterLayout`](../Source/PluginProcessor_APVTS.cpp). Every preset
save / load / sync path iterates this single table.

### Parameter kinds (v2)

Each `RhythmParamDef` carries a `ParamKind` tag that drives how the property
is serialised:

| `ParamKind`         | XML form (v2)                                  | Example                              |
|---------------------|------------------------------------------------|--------------------------------------|
| `Float`             | actual de-normalised value as a number         | `r0_fltCut="234.7"`                  |
| `Int`               | integer                                        | `r0_stepsA="16"`                     |
| `Bool`              | string `"true"` / `"false"`                    | `r0_aEnvLeg="true"`                  |
| `AlgorithmIndex`    | stable algorithm name from name table          | `r0_drvChar="Bitcrusher"`            |

For `AlgorithmIndex` kinds the name tables live in
[Source/Audio/AlgorithmNames.h](../Source/Audio/AlgorithmNames.h). The two
tables there are `kInsertAlgorithmNames` (driveChar 0..13) and
`kFilterTypeNames` (filterType 0..15). Effect / Reverb algorithm IDs come
from `FXAlgorithmRegistry` in
[Source/FX/FXAlgorithmDef.h](../Source/FX/FXAlgorithmDef.h) but are not yet
written as names (they're still integer-indexed — Stage 35 followup).

### Algorithm-name rules

The contract for algorithm name strings is strict, because old presets must
keep loading across reorders / insertions / removals of algorithms:

- **Names never change after a release.** A typo fix or rename requires an
  alias entry pointing the old name at the same index, so existing presets
  still resolve.
- **Names are append-only at the table level** but the index they map to is
  set by the dispatch table in the audio code. You can reorder the audio
  dispatch — saved presets re-resolve by name, not index.
- **Names are ASCII, no spaces** — they're XML attribute values and want to
  be hand-edit-friendly.
- **An unknown name silently falls back to integer parsing.** A pre-Stage-35
  preset with `r0_drvChar="4"` still loads correctly as Bitcrusher.

### Modulators child

The `<Modulators>` subtree contains:

- `<Seq id="csN" ...>` — one per `ControlSequence`. Properties: `mode`,
  `polarity`, `loopNV` / `loopMod` / `loopMult` (loop timing), `stepNV` /
  `stepMod` / `stepMult` (step timing). Children: `<Step v="..."/>` for
  step values, `<Point x="..." y="..."/>` for Bézier curve points. Curved
  segments also carry `bez="1" hx="<float>" hy="<float>"` on the `<Point>`
  where `hx`/`hy` are the Bézier handle offsets; absent when curve type is
  Stepped (mode=1).
- `<Asgn id="..." src="..." dest="..." depth="..." curve="..."/>` — one
  per modulation assignment. `src` is a CS output (`cs0_output`) or another
  assignment's depth (`assign_{id}_depth`); `dest` is a destination ID from
  the [Modulation/ModulationDestinations.h](../Source/Modulation/ModulationDestinations.h)
  table.

On load, source / destination IDs are validated against the live registry
(#437). Assignments with unknown IDs are dropped and reported to the user
via `onLoadError` rather than silently no-op'd.

## `MuClidPreset` schema

```xml
<MuClidPreset presetName="My Session" presetCategory="Drums" presetVersion="2"
              presetDescription="..." presetEmbedSamples="1">
  <Rhythm name="Kick" colour="3" sample="kick.wav"
          stepsA="16" hitsA="4" ...
          drvChar="Bitcrusher" ...>
    <Modulators>...</Modulators>
  </Rhythm>
  <Rhythm name="Snare" .../>
  <!-- ... up to 8 rhythms ... -->
  <GlobalState eff_algo="0" eff_en="1" eff_send="1.0" eff_p0="0.5" ...
               dly_en="1" dly_ms="250.0" ...
               rev_algo="0" rev_en="1" ... />
</MuClidPreset>
```

- `Rhythm` children carry per-rhythm state (same suffixes as `MuClidRhythm`
  but without the `r0_` prefix).
- `GlobalState` child carries FX / mixer state — the IDs in
  `mu_pp::kGlobalParams` from
  [PluginProcessor_Internal.h](../Source/PluginProcessor_Internal.h).
- In v2 these are written as actual de-normalised values. Algorithm-name
  strings for `mst_insChar` / `mst_ins2Char` / `eff_algo` / `rev_algo` are
  not yet emitted — those write as integer indices. A Stage 35 followup
  will add a parallel `kGlobalParamDefs` table to fix this.

### Sample handling

Each `Rhythm` child can reference its sample two ways:

- `sample="filename.wav"` (relative) or `sample="C:/path/to/file.wav"` (absolute).
  Load first tries resolving a relative name against `<contentDir>/Samples/`
  silently. An absolute path is tried directly, then falls back to
  `<contentDir>/Samples/<basename>` if the original path is missing, with an
  `onLoadError` warning so the user can notice mis-matched same-basename files.
  Shipped presets always use filename-only relative references.
- `sampleData="<base64>" sampleName="file.wav"` — sample bytes embedded
  directly. Decoded to `%TEMP%/muClid_samples/<sampleName>` on load. The
  save path detects temp-dir paths and force-embeds (#439) so an ephemeral
  decode never gets written back as a permanent reference. When `sampleData`
  is present it always takes priority over `sample`.

## Stable contracts

These are the invariants the format relies on for forward compatibility:

1. **APVTS parameter IDs never change** (e.g. `r0_drvChar`, `eff_algo`,
   `mst_insChar`). The string IDs are baked into every saved preset and
   every DAW project's automation lanes.
2. **`presetVersion` is bumped on schema-breaking changes.** The previous
   reader path stays in the code (legacy migration) until the next major
   release.
3. **Algorithm names are append-only.** The dispatch-table index can move
   freely; the user-facing name string must not.
4. **`kRhythmParamDefs` is the single source of truth** for per-rhythm
   parameter wiring. APVTS layout, applyRhythmSuffix, pushRhythmToAPVTS,
   and the v2 preset reader / writer all consume it.
5. **Modulation source / destination IDs are validated on load** (#437) —
   stale IDs report via `onLoadError` instead of dangling silently.

## Stage 35 — done

All five steps shipped: algorithm name tables (Step 1), C++ field rename
`drive*` → `insert*` (Step 2, #452), de-normalised save format (Step 3),
v2-only load path (Step 4), this document (Step 5). Global-state algorithm
names landed via #451. Modulator enum-name serialisation landed via #436.
Legacy v0 / v1 reader code was removed when it became clear the maintenance
cost outweighed the user value — hand-converted any pre-v2 presets via the
dev. The `presetVersion` attribute stays in saved files so a future format
break can either reject older files cleanly or re-add a migration ladder.

## Follow-up

- **Preset round-trip test harness** (#442 + sub-issues #453-#457): a save
  → load → save cycle should produce byte-equivalent output for every
  parameter. Sub-issues split out the remaining coverage:
  - `#453` kGlobalParamDefs round-trip
  - `#454` writeKindedProperty / readKindedPropertyAsActualV2 round-trip
  - `#455` serialiseModulators / deserialiseModulators round-trip
  - `#457` algorithm-name table size sanity
