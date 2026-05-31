# mu-Clid — CLAUDE.md

Product-specific guidance for working in `mu-clid/`. Read alongside the family-wide [/CLAUDE.md](../CLAUDE.md).

## Project overview

**μ-Clid** is a JUCE/C++ Euclidean rhythm sequencer and sample trigger plugin (VST3 + CLAP + Standalone, plus a Lite MIDI-effect variant) by Transwarp Development Project. Up to 8 polyrhythmic rhythm slots, each with two euclidean hit generators, sample playback, voice chain (ADSR + filter), up to 8 drawable LFO/step modulators, and a shared FX chain (Effect/Delay/Reverb) with mixer.

## Build targets

- `mu-clid` — full plugin (VST3 + CLAP + Standalone)
- `mu-clid-lite` — single-rhythm MIDI-effect variant (VST3 + CLAP)
- `mu-clid-tests` — console UnitTest runner; **`EXCLUDE_FROM_ALL TRUE`**, built on demand via `cmake --build build --target mu-clid-tests`. **Deliberate divergence from mu-tant-tests** (which builds every default build): mu-clid's Release build deploys to testers via OneDrive, and compiling the heavy 140-test suite (links `juce_audio_processors` + many cpps) on every tester-deploy build costs time for no deploy benefit. mu-tant is small + Debug-only/local, so building its tests every time is cheap. This is a build-speed concession, not family-consistency drift — keep the split unless the deploy-build cost stops mattering.

Release build of `mu-clid_Standalone` triggers the OneDrive post-build deploy to `MUFAMILY_WIN_DIST`. Debug skips deploy. Lite installer requires Inno Setup.

## Source layout

```
mu-clid/Source/
├── Plugin/           PluginProcessor, PluginEditor (extends EditorShellBase),
│                     PresetIO + PresetIO_HostState, HotSwapStager + HotSwapBoundary,
│                     ModulationSkew, RenderMode, StandaloneApp, LiteEditor, SamplePreview
├── Sequencer/        Rhythm, HitGenerator, SequencerEngine, EuclideanGenerator
├── UI/               Euclidean panels + VoiceSection (Pitch/Filter/Amp
│                     subsections; the Insert subsection + the ModulatorPanel /
│                     ModMatrixPanel / ModulatorEditor / MixerOverlay are shared
│                     from mu-core), RhythmCircle, RhythmMiniVisual, RhythmSidebar,
│                     RhythmPanel, SampleBrowser, SettingsOverlay, MasterLoopSection
├── Persistence/      PresetMigrations + helpers
├── License/          LicenseChecker
└── Tests/            juce::UnitTest suite
```

The editor shell (TransportBar, About, Save, PresetBrowser, MIDI-preset panels, StatusBar, demo banner, overlay state machine, layout, keybindings) lives in `mu-core/UI/EditorShellBase.h` — `PluginEditor` extends it and supplies sidebar (`RhythmSidebar`) + main panel (`RhythmPanel`) + mixer overlay + settings overlay.

## Design documents

| Sub-doc | When to read |
|---|---|
| [docs/mu-clid/design-sequencer.md](../docs/mu-clid/design-sequencer.md) | Euclidean params, DAW sync, control sequence params, modulation signal flow |
| [docs/mu-clid/design-voice.md](../docs/mu-clid/design-voice.md) | Voice chain, ADSR, filter, interpolation quality, sample handling, time stretching (TimeStretcherBase) |
| [docs/mu-clid/design-ui.md](../docs/mu-clid/design-ui.md) | μ-Clid specific panel layouts — RhythmCircle, EuclideanPanel, Mixer, Transport. Defers to design-ui-family.md for colours/sizes. |
| [docs/mu-clid/design-presets.md](../docs/mu-clid/design-presets.md) | APVTS wiring plan, preset storage, save/restore |
| [docs/mu-clid/preset-format.md](../docs/mu-clid/preset-format.md) | **Preset format reference** — `.muRhythm` / `.muClid` XML schemas, versioning, ParamKind tags, algorithm-name contracts. Read when editing presets by hand or adding a new persisted parameter. |
| [docs/mu-clid/TestPlan.md](../docs/mu-clid/TestPlan.md) | 25-step manual smoke walkthrough of the μ-Clid standalone UI. |
| [docs/mu-clid/archive/](../docs/mu-clid/archive/) | Closed μ-Clid stage plans + one-shot audits. Read only if revisiting historical decisions. |
| [docs/mu-clid/design.md](../docs/mu-clid/design.md) | Full original μ-Clid spec — only read if the sub-docs don't cover it |

## Work tracking

No stages are scheduled — mu-clid is feature-complete through Stage 34. New work is tracked as numbered issues in [backlog.md](../backlog.md), grouped Open → On Hold → Fixed.

## mu-clid-specific architectural rules

(See family-wide rules in [/CLAUDE.md](../CLAUDE.md). These add to those.)

- **Each rhythm is its own APVTS subtree** — preset save/load + per-rhythm hot-swap rely on the subtree structure.
- **Hot-swap uses `suspendProcessing` + `std::move`** — new `Rhythm` + `VoiceEngine` are staged on the message thread (`stageRhythmPreset`), committed atomically at the next loop boundary under a single suspend per `handleAsyncUpdate` call (multi-rhythm bursts batch into one suspend). The "atomic pointer hot-swap" pattern (lock-free pointer exchange + message-thread cleanup queue for the old engine) is a v2 candidate if perceptible suspend-gap clicks become a complaint — current single-block silence gap (~1.33 ms at 48k/64) is short enough to be inaudible on percussive material, occasionally audible on sustained pads.
- **RhythmSidebar item order supports variable ordering from day one** — required for v2 drag-to-reorder.

## Key mu-clid patterns

### PluginProcessor default rhythm
`PluginProcessor` constructor always creates one default rhythm (16 steps, 4 hits). Never add a rhythm unconditionally in `PluginEditor` — check `getNumRhythms() == 0` first (the sidebar constructor calls `refreshItems()` which also reads the existing rhythm).

### RhythmCircle sizing
All ring radii are computed proportionally from `min(width, height) / 2 - margin`. Ring A outer = maxR, width = `maxR * 0.20`. Ring B starts at `ring_A_inner - gap`. Same for ring C (dashed). This makes the circle look correct at both the large (200px) panel size and the small (sidebar ~50px) size.

### Pre-APVTS data binding (historical)
Before Stage 10, panels mutated `Rhythm` data directly (e.g. `rhythm->genA.steps = (int)v`) and called `proc.updatePattern(index)` to refresh the cached pattern. All parameters are now wired through APVTS — older code patterns referencing direct field mutation are stale.
