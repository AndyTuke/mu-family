# μ-Clid — Preset & State System Design Reference

## Plugin State

- Full state stored in APVTS `ValueTree` — serialised by JUCE `getStateInformation` / `setStateInformation`
- Each rhythm lives in its own ValueTree subtree — enables per-rhythm preset save/load and hot-swap
- DAW project save/restore is automatic — full state restored on project open, samples reloaded immediately
- Preset name shows asterisk (*) when current state differs from last saved preset
- Fresh instance: loads user default preset if set, otherwise factory demo patch
- Factory demo patch: 2–3 rhythms with euclidean patterns, no samples loaded, sequencer runs but silent
- New rhythm default: Euclid A with 0 hits, OR logic, no sample, fader at -6dB, no modulators active

## Content Folder (Issues #67–#70)

| Item | Default path (Windows) |
|---|---|
| Root | `%USERPROFILE%\Documents\TDP\muClid\` |
| Full presets | `…\Presets\` |
| Rhythm presets | `…\Rhythms\` |
| User samples | `…\Samples\` |

- All three paths are user-changeable in the Settings overlay and persisted in `juce::ApplicationProperties`
- Folder is created on first launch if missing (no error if creation fails — just warn in log)

## File Extensions

| Type | Extension | Format |
|---|---|---|
| Full preset | `.muclid` | JUCE ValueTree → XML |
| Rhythm preset | `.muRhyth` | JUCE ValueTree → XML (single rhythm subtree) |

## Sample Embedding

- Save dialog shows a checkbox **"Embed samples"** (default: off)
- When checked: each sample is base64-encoded and stored as a `<sample>` child in the XML
- On load: embedded samples are extracted to a temp directory, then loaded normally
- When unchecked: samples are stored as absolute paths with a fallback search in the user Samples folder

## Default Preset / Rhythm

- On plugin load: reads `Presets\_default.muclid` — restores silently; if missing, creates an empty fresh state (no error)
- On new rhythm added: reads `Rhythms\_default.muRhyth` — applies silently; if missing, creates a blank rhythm
- Users can **File → Save as Default** to overwrite `_default.muclid` / `_default.muRhyth`

## Preset Storage

- Patch presets: JUCE ValueTree serialised to XML in user content folder (`.muclid`)
- Rhythm presets: same format, single rhythm subtree (`.muRhyth`)
- Default preset: `_default.muclid` / `_default.muRhyth` in the respective subfolders
- Factory presets: shipped with plugin, stored in plugin resources, restorable from Settings

## Preset Browser (Stage 10)

- Search covers name and description
- Category filters: techno, perc, ambient, experimental + all (single-select)
- Right-click preset → context menu: Load, Set as default (★ indicator), Delete
- Save dialog: name input + description input + category selector + cancel/save
- Rhythm preset preview: circle showing hit pattern + sample filename

## APVTS Parameter Naming

All host-automatable parameters live in APVTS. Actual ID format:
- Rhythm parameters: `r{N}_{suffix}` e.g. `r0_stepsA`, `r0_hitsA` — flat at the APVTS root, not a per-rhythm subtree.
- FX parameters: `eff_{param}`, `dly_{param}`, `rev_{param}`, `echo_{param}`, plus `eff2dly` / `eff2rev` / `dly2rev` for intra-FX sends.
- Mixer channel parameters: `ch{N}_{param}` (rhythm channels), `ret_eff_{param}` / `ret_dly_{param}` / `ret_rev_{param}` (returns), `mstr_lvl` / `mstr_pan` / `mstrLoop` (master).
- Master insert: `mst_ins{Char,Drv,Out,Bits,Rate,Dit,Ton,Mid}` and `mst_ins2{…}` for the chained second slot.

Parameter IDs are strings in ModulationMatrix — this is what makes new sources/destinations automatic without refactoring. Modulator state itself (LFO curves, step values, matrix assignments) is **not** APVTS-backed — it serialises to a `<Modulators>` child of the ValueTree alongside APVTS, save-roundtripped by `serialiseModulators` / `deserialiseModulators`.

## Current State (post-Stage 10 — hybrid APVTS + manual wiring)

State saves and restores correctly between DAW sessions, host automation is supported, and v2 preset files round-trip every parameter via `kRhythmParamDefs` / `kGlobalParamDefs` (the single declarative tables in `Source/Persistence/RhythmParamTable.h` and `Source/Persistence/PresetHelpers.h`).

UI panels do **not** use JUCE's `SliderAttachment` / `ButtonAttachment` / `ComboBoxAttachment` — the project's custom `KnobWithLabel` / `SegmentControl` / `DropdownSelect` controls don't derive from `juce::Slider` etc., so attachments don't apply. Each panel instead:
1. Writes user changes through with `p->setValueNotifyingHost(p->convertTo0to1(v))` from the control's `onValueChanged` lambda (manual one-way push).
2. Subscribes to APVTS via `addParameterListener` and bounces a per-suffix refresh back to the message thread in `parameterChanged` so DAW automation reflects in the UI.
3. Reads display values directly from the `Rhythm` struct in `loadFromRhythm` / `refreshSuffix` — APVTS feeds the engine via `syncRhythmParam`, which mutates `Rhythm.voiceParams`; the panel reads the resulting struct rather than the APVTS atomic.

The per-rhythm panel registers listeners through `RhythmPanel::registerRhythmListeners`; mixer subscribes through `MixerOverlay::isMixerRelevantParam`. Adding a new APVTS parameter requires touching three sites: `createParameterLayout` (registration), `kRhythmParamDefs` or `kGlobalParamDefs` (preset round-trip + Rhythm sync), and the owning panel's listener table + `refreshSuffix` handler.
