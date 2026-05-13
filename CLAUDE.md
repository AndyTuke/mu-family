# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build workflow

After every build, read `backlog.md` and fix open (unchecked) issues immediately, without asking, up to a miximum of 5 issues. Prioritise issues that are related to the current stage

After every response, if any issues in `backlog.md` have been changed status or new issues have been added, update `backlog.md` immediately to reflect the current state.

New feature ideas live in `docs/design-future.md` under **Unscheduled Ideas**. Ask the user before implementing any of them.
Update the stages as they up worked on, move to development history when done

## Git commit messages

Every commit message must include:
1. **Stage(s)** — which development stage(s) are included in this commit (e.g. `Stage 12`, `Stages 12–13`)
2. **Issues closed** — list each issue number and its one-line description (e.g. `Closes #12: rhythm rename propagation`)
3. **Full version** — the version string in the form `v1.0.<build>` using the current value from `build_number.txt` (e.g. `v1.0.103`)

Example commit message format:
```
Stage 13: UI completions — Amp FX sends, intra-FX wiring verified, Settings Overlay audit

Closes #17: Amp FX send knobs (Effect/Delay/Reverb) added to Voice Amp row
Closes #22: Intra-FX APVTS wiring verified end-to-end
Closes #23: Settings Overlay audited against design spec

Version: v1.0.103
```

## Design documents

The full design is split into focused sub-documents. **Read only the relevant one** rather than the monolithic design.md.

| Sub-doc | When to read |
|---|---|
| [docs/design-sequencer.md](docs/design-sequencer.md) | Euclidean params, DAW sync, control sequence params, modulation signal flow |
| [docs/design-voice.md](docs/design-voice.md) | Voice chain, ADSR, filter, interpolation quality, sample handling, time stretching (TimeStretcherBase) |
| [docs/design-fx.md](docs/design-fx.md) | FX algorithms, delay, reverb, intra-FX routing, FXSlotBase interface |
| [docs/design-ui-family.md](docs/design-ui-family.md) | **Shared design system** — colour tokens, typography, control sizes, interaction patterns, shared module plan. Read this before any UI work. |
| [docs/design-ui.md](docs/design-ui.md) | μ-Clid specific panel layouts — RhythmCircle, EuclideanPanel, Mixer, Transport. Defers to design-ui-family.md for colours/sizes. |
| [docs/design-presets.md](docs/design-presets.md) | APVTS wiring plan, preset storage, save/restore, current pre-APVTS state |
| [docs/design-future.md](docs/design-future.md) | Unscheduled future ideas — read to avoid closing off options during current stages |
| [docs/design.md](docs/design.md) | Full original spec — only read if the sub-docs don't cover it |

---

## Project overview

**μ-Clid** is a JUCE/C++ Euclidean rhythm sequencer and sample trigger plugin (VST3 + Standalone) by Transwarp Development Project. Up to 8 polyrhythmic rhythm slots, each with two euclidean hit generators, sample playback, voice chain (ADSR + filter), up to 8 drawable LFO/step modulators, and a shared FX chain (Effect/Delay/Reverb) with mixer.

## Prerequisites

JUCE is not vendored. Set `JUCE_PATH` to a local JUCE checkout before configuring:

```powershell
$env:JUCE_PATH = "D:\JUCE"
```

## Build commands

```bash
cmake -B build                              # Configure (once, or after CMakeLists changes)
cmake --build build --config Debug          # Debug build
cmake --build build --config Release        # Release build
cmake --build build --config Debug && cmake --build build --config Release  # Full build (both)
```

Artefacts land in `build/mu-clid_artefacts/Debug/` or `.../Release/`.
Always build Release when producing tester builds — the CMake post-build hook automatically deploys the VST3, CLAP, and Standalone to the OneDrive tester folder (`MUCLID_WIN_DIST` in CMakeLists.txt). Debug builds skip the deploy step.

## Development history

Stages 1–31 are complete. See [docs/DevelopmentHistory.md](docs/DevelopmentHistory.md) for the full stage-by-stage log with dates. When a stage is completed, it should be moved to the DevelopmentHistory.md file.

## Upcoming stages

All work below resolves open issues from [backlog.md](backlog.md). Issues are referenced by number.
The backlog in `backlog.md` must always be grouped: Open → On Hold → Fixed. Within each group, items must be ordered by issue number descending (highest first). Every backlog update must preserve this ordering.
All code changes must be logged as backlog entries to maintain a complete development history.

| Stage | Status | Scope | Issues |
|---|---|---|---|
| 32 | 🔴 Open | **Mixer Responsive Layout** — horizontal channel-width scaling with window width; vertical send/pan/fader proportionality within each strip; FX area height proportional to window; FX row knob widths fill available space. Design: see [docs/design-ui.md](docs/design-ui.md) Mixer section. | #255, #256, #257 |
| 33 | 🔴 Open | **Plugin Family Architecture (mu-core)** — CMake `mu-core` STATIC library extracting shared audio/FX/modulation/UI; `Rhythm` → `VoiceSlot` base generalisation; `ProcessorBase` shared processor skeleton; `MuClidLookAndFeel` → `MuLookAndFeel` rename. Enables mu-tant and future μ-family plugins to share the voice/modulation/mixer stack without code duplication. Design: [docs/design-plugin-family.md](docs/design-plugin-family.md). | #258, #259, #260, #261 |


## Source layout (actual, as built)

`Source/` top-level: `PluginProcessor`, `PluginEditor`, `Sequencer/`, `Audio/`, `Modulation/`, `FX/` (slots + `Effects/`), `UI/` (panels + `Components/`). Use Glob/Explore to navigate — the tree is derivable from the filesystem.

## Critical architectural rules

- **Everything in APVTS** — if it's not in the ValueTree it won't save. Each rhythm in its own subtree. All parameters are wired through APVTS.
- **Audio thread never allocates** — all allocation in `prepareToPlay`, never in `processBlock`.
- **ModulationMatrix is the single reader** — audio engine reads only from ModulationMatrix, never directly from APVTS or ControlSequence.
- **Rhythms are fully self-contained** — ControlSequences may only target parameters within their own rhythm. No cross-rhythm modulation. Global FX parameters are not valid modulation destinations.
- **ControlSequence lengths are independent** — never couple loop lengths or rates to rhythm step counts.
- **FXSlotBase interface for all FX** — enables VST3 plugin hosting in v3 without refactoring.
- **TimeStretcherBase wraps the time-stretch engine** — currently a stub; SoundTouch (v1) or RubberBand (v2) slots in without refactoring.
- **Atomic pointer for rhythm hot-swap from day one** — required for v2 live swap feature.
- **RhythmSidebar item order supports variable ordering from day one** — required for v2 drag-to-reorder.
- **All colours and sizes in MuClidLookAndFeel only** — no hardcoded values in component drawing code.
- **All UI uses the shared component library** — never build a one-off version of a standard control.
- **ModulationMatrix processes in dependency order** — detects and rejects circular dependencies at assignment creation time.
- **Time-stretch DLL (SoundTouch/RubberBand) ships separately** — required for LGPL/GPL compliance when implemented.

## Key patterns discovered during implementation

### KnobWithLabel callbacks
`KnobWithLabel` has **two** separate callbacks:
- `onStatusUpdate(name, valueString)` — called automatically from the internal `slider.onValueChange` for status bar display
- `onValueChanged(double)` — also called from the same `slider.onValueChange` lambda; use this for data mutation in panels like `EuclideanPanel`

Never override `getSlider().onValueChange` directly — it replaces both callbacks. Always use `onValueChanged` for data binding.

### Pre-APVTS data binding
Until Stage 10, panels mutate `Rhythm` data directly (e.g. `rhythm->genA.steps = (int)v`). After mutation, call `proc.updatePattern(index)` to refresh the cached pattern in `SequencerEngine`. This is intentional and correct for now.

### PluginProcessor default rhythm
`PluginProcessor` constructor always creates one default rhythm (16 steps, 4 hits). Never add a rhythm unconditionally in `PluginEditor` — check `getNumRhythms() == 0` first (the sidebar constructor calls `refreshItems()` which also reads the existing rhythm).

### RhythmCircle sizing
All ring radii are computed proportionally from `min(width, height) / 2 - margin`. Ring A outer = maxR, width = `maxR * 0.20`. Ring B starts at `ring_A_inner - gap`. Same for ring C (dashed). This makes the circle look correct at both the large (200px) panel size and the small (sidebar ~50px) size.

## Third-party libraries

| Library | Purpose | Notes |
|---|---|---|
| JUCE | Core framework | Via `JUCE_PATH` env var |
| Signalsmith Reverb | Room/hall/plate reverb | MIT, header-only |
| Monocypher | License key crypto | BSD-2-Clause, compiled in |
| clap-juce-extensions | CLAP format support | MIT, compiled in |
| SoundTouch | Time stretching (v1, planned) | LGPL — will ship as DLL when implemented |
| RubberBand | Time stretching (v2, planned) | Wrapped behind `TimeStretcherBase` — no refactor needed when upgrading |

## UI values

Knob colour coding, ring colour coding, window sizing, and all layout constants are in [docs/design-ui.md](docs/design-ui.md).
