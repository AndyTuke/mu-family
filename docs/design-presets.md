# μ-Clid — Preset & State System Design Reference

## Plugin State

- Full state stored in APVTS `ValueTree` — serialised by JUCE `getStateInformation` / `setStateInformation`
- Each rhythm lives in its own ValueTree subtree — enables per-rhythm preset save/load and hot-swap
- DAW project save/restore is automatic — full state restored on project open, samples reloaded immediately
- Preset name shows asterisk (*) when current state differs from last saved preset
- Fresh instance: loads user default preset if set, otherwise factory demo patch
- Factory demo patch: 2–3 rhythms with euclidean patterns, no samples loaded, sequencer runs but silent
- New rhythm default: Euclid A with 0 hits, OR logic, no sample, fader at -6dB, no modulators active

## Preset Storage

- Patch presets: JUCE ValueTree serialised to XML in user documents folder
- Rhythm presets: same format, single rhythm subtree
- Default preset: stored in `juce::ApplicationProperties` / `PropertiesFile` (global, not per-project)
- Factory presets: shipped with plugin, stored in plugin resources, restorable from Settings

## Preset Browser (Stage 10)

- Search covers name and description
- Category filters: techno, perc, ambient, experimental + all (single-select)
- Right-click preset → context menu: Load, Set as default (★ indicator), Delete
- Save dialog: name input + description input + category selector + cancel/save
- Rhythm preset preview: circle showing hit pattern + sample filename

## APVTS Parameter Naming

All parameters must be in APVTS. Suggested ID format:
- Rhythm parameters: `rhythm_{index}_{param}` e.g. `rhythm_0_stepsA`, `rhythm_0_hitsA`
- FX parameters: `fx_effect_{param}`, `fx_delay_{param}`, `fx_reverb_{param}`
- Mixer parameters: `mixer_rhythm_{index}_{param}`, `mixer_master_{param}`
- Modulation: `rhythm_{index}_mod_{mod_index}_{param}`

Parameter IDs are strings in ModulationMatrix — this is what makes new sources/destinations automatic without refactoring.

## Current State (Stage 6 — pre-APVTS)

APVTS is not yet wired. UI currently reads/writes `Rhythm` data directly. This is intentional — APVTS wiring is Stage 10. Until then:
- State does not save between DAW sessions
- Automation does not work
- The direct data binding (e.g. `rhythm->genA.steps = (int)v`) is the correct pattern for now
- Stage 10 will replace direct reads/writes with APVTS parameter attachments
