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

All parameters must be in APVTS. Actual ID format:
- Rhythm parameters: `r{N}_{param}` e.g. `r0_stepsA`, `r0_hitsA`
- FX parameters: `fx_effect_{param}`, `fx_delay_{param}`, `fx_reverb_{param}`
- Mixer parameters: `mixer_rhythm_{index}_{param}`, `mixer_master_{param}`
- Modulation: `r{N}_mod_{mod_index}_{param}`

Parameter IDs are strings in ModulationMatrix — this is what makes new sources/destinations automatic without refactoring.

## Current State (Stage 10+ — APVTS wired)

All parameters are wired through APVTS (completed Stage 10). State saves and restores correctly between DAW sessions, and automation is supported. All UI panels use APVTS parameter attachments rather than direct `Rhythm` data writes.
