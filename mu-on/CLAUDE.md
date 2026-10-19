# μ-On — CLAUDE.md

Product-specific guidance for working in `mu-on/`. Read alongside the family-wide [/CLAUDE.md](../CLAUDE.md).

## Project overview

**μ-On** is a JUCE/C++ **909-style groove sequencer** by Transwarp Development Project. One to eight
**tracks** (the shared "layers"; a fresh instance has one Kick track), each running one of five
**engines** chosen with the Engine dropdown: **Kick** (synthesis), **Bass** (deep synth — the focus),
**Hat** (sample), **Snare** (sample), or **Rumble** (a techno "rumble" that processes another track's
audio, the Source dropdown, rather than stepping). Every track has a row of 16 steps on the grid.
Builds Standalone + VST3 + CLAP (+ AU on macOS via the family rule). Release builds stay local, like
the rest of the family.

The editor inherits `mu-core/UI/EditorShellBase.h` and uses the shared `LayerSidebar` + `LayerPanel` +
`LayerEditController` + `MixerOverlay`, so the TransportBar / StatusBar / About / window sizing /
`MuLookAndFeel`, add / delete / reorder and the whole mixer/FX rack come for free. The product
supplies the five engines, the 909 sequencer, and the content band (`TrackContent`).

## Current scope

**Implemented**
- **Variable tracks** — `mu_limits::kMaxLayers` (8) `Track` objects, parameters and mixer strips exist for the processor's lifetime (`keepSpareLayers`); only `numLayers` is runtime (the shared `ProcessorBase::addLayer` / `removeLayer` / `swapLayers`, `createLayer(slot)`, `layersMoved`, `layerAdded`). `processBlock` takes `ScopedTryLock(layersLock)` once, skips while `layerEditInFlight`, and everything that touches the tracks or engines (sequencer, params, render) is inside it. `getTrack(i)` is the typed accessor; `getTrackEngine(i)` reads `t{N}_eng`. A track is named after its engine (not saved).
- **Engines** ([Source/Audio/](Source/Audio/)) — `KickEngine` (synth), `BassEngine` (osc+sub+`MultiModeFilter`+drive), `SampleChannel` (Hat/Snare via mu-core `SamplePlayer`, procedurally-generated default one-shots), `RumbleEngine` (drive → tempo-synced delay taps → dark reverb → drawable volume envelope (length set by the shared Loop control, `NoteLengthControl`, held per track) → final filter, fed the source track's render). Each `Track : Layer` holds its engines as `TrackEngine`s (`Audio/TrackEngine.h`; an unlocked track holds all five prepared engines and plays the one `t{N}_eng` names, in the same block), resolves the selected engine's params **through its own `ModulationMatrix`** (proportion-space seed → matrix → write-back; try-lock, zero-cost when unused), and renders through the shared `renderAndMix` driver, non-Rumble tracks first and then the Rumble tracks. See [docs/mu-on/design-engine.md](../docs/mu-on/design-engine.md).
- **Saved data** — per track `t{N}_eng` (choice) and `t{N}_src` (Rumble source, a layer index; follows its layer through delete / reorder via `remapLayerReferences`, a gone source becomes the first Kick), then every engine's params **engine-qualified** `t{N}_kick_*`, `t{N}_bass_*`, `t{N}_hat_*`, `t{N}_snare_*`, `t{N}_rumble_*` ([Source/Persistence/TrackParams.{h,cpp}](Source/Persistence/TrackParams.{h,cpp}): `EngineType`, the id helpers). `numVoices` / `voiceColours` (shared, in `ProcessorBase`). A state without `numVoices` is a five-lane state: `toLayerState` migrates lane *i* to layer *i* running engine *i* (rows renamed, `eng` / `src` written, colours `0,2,1,3,4,5,6,7`); old `k_ b_ h_ s_ r_` ids and MIDI maps go through the legacy-prefix mechanism; an old `.muTrack` (rows unqualified, `lane` attribute) is migrated on load.
- **909 sequencer** ([Source/Sequencer/](Source/Sequencer/)) — `StepPattern` (8×16, on/accent, atomic cells; each row is a `StepRow` owned by its `Track` and bound by slot after every structural edit; kept when the engine changes), `GrooveSequencer` (sample-accurate step-edge triggering off the internal beat, swing, accent velocity). A Rumble track ignores its row. See [docs/mu-on/design-sequencer.md](../docs/mu-on/design-sequencer.md).
- **Per-track modulation** ([Source/Modulation/MuOnModDest.h](Source/Modulation/MuOnModDest.h)) — each track is a `Layer` (`ControlSequence` × N + `ModulationMatrix`); `MuOnModDest` has one destination table per **engine** (Kick/Bass/Hat/Snare/Rumble expose different params; the editor swaps the provider when the shown track's engine changes and `selectTrackEngine` drops assignments aimed at the old engine). Destination ids end in `.prop` → proportion-space depth, so a full-depth mod sweeps the whole knob. Modulators persist in each track's layer node.
- **Editor** — the shared `LayerPanel` (header strip, content band, modulators) holding [TrackContent](Source/UI/TrackContent.h) (the shown track's ENGINE box with the Engine dropdown, the Rumble's Source, the engine's `ParamKnobGrid`; over the GROOVE | STEPS panel with Swing / Accent or the Rumble envelope and the [StepRows](Source/UI/StepRows.h), one 16-step row per track). Ruling and sizes: [docs/mu-on/design-ui.md](../docs/mu-on/design-ui.md) "With variable layers".
- **Bass↔Kick sidechain** — the shared `MixerEngine` channel-to-channel sidechain, set up in the mixer; there is no pre-wired default and no Duck knobs on the engine panel (Andy 2026-10-08). A five-lane state's saved mixer values load as saved.
- **Mixer/FX rack** — channel strips + global FX via `mu_mixfx::addGlobalFxParams`, synced through `ProcessorBase::syncGlobalFxParam` + a `parameterChanged` listener (mirrors mu-toni).
- **Internal free-running transport** drives the sequencer; **APVTS state** carries everything (`.muOn` full / `.muTrack` per-track extensions wired; a track preset carries its engine and loads into any track).
- **Tests** ([Source/Tests/](Source/Tests/)) — ApvtsLayout, Sequencer (beat→step, firing, swing, accent, serialise), Engine (kick/hat sound + decay), Modulation (`.prop` scale, engine-scoped dest tables, modulator round-trip), ProcessorSeam (fresh = one Kick track, fill to 8, delete / reorder with the Rumble source following, `runStructuralEditStress`), PrefixMigration + EngineMigration (five-lane states, APVTS dumps, MIDI maps and track presets load as engine-qualified layers). Built every build (mirrors mu-tant/mu-toni-tests).

**Not yet implemented** (see the design docs' "Future" sections)
- Per-step **pitch** for the bass (unlocks glide + note-off → Release; these are intentionally not exposed yet so no inert controls are shown).
- **Real sample loading** ("Load .wav…") + shipped factory `.wav` (the `SampleChannel` buffer is the only seam; gated on code-signing like #99).
- Pattern **bank + chaining** (A/B + song mode), variable pattern length, sample-accurate onset.
- Renaming a track (a track's name follows its engine).

## Source layout

```
mu-on/Source/
├── Plugin/      PluginProcessor (ProcessorBase: APVTS, mixer/FX, sequencer, render dispatch, track edits),
│                PluginProcessor_Preset (state migration, Track persistence), PluginEditor (EditorShellBase),
│                StandaloneApp (shared confirmQuitAsync), TrackParams.h (EngineType + id helpers)
├── Audio/       KickEngine, BassEngine, SampleChannel, RumbleEngine, TrackEngine (per-track engine variant)
├── Sequencer/   StepPattern (+ StepRow), GrooveSequencer, Track (the layer type)
├── Modulation/  MuOnModDest (per-engine modulation-destination tables)
├── UI/          TrackContent (content band), StepRows (the step rows), TrackMiniVisual
└── Tests/       TestMain + ApvtsLayout + Sequencer + Engine + Modulation + Preset + SlotState + ProcessorSeam + PrefixMigration + EngineMigration
```

Mirrors the family `{Plugin, Audio, Sequencer, UI, Persistence, Tests}` topology (Persistence/License
land when preset I/O / licensing do).

## Design documents

| Sub-doc | When to read |
|---|---|
| [docs/mu-on/design-engine.md](../docs/mu-on/design-engine.md) | The five engines (incl. Rumble), per-slot engines and render order, the bass deep-clean↔rumble range, the sidechain, RT-safety, the bass deepening roadmap. |
| [docs/mu-on/design-ui.md](../docs/mu-on/design-ui.md) | The editor: sidebar + engine box + step rows, the variable-layer ruling. |
| [docs/mu-on/design-sequencer.md](../docs/mu-on/design-sequencer.md) | StepPattern model, clocking/swing/accent, the grid UI, future pattern chaining. |

## Conventions in force
- All product symbols under `mu_on::` (boundary check `tests/scripts/check-core-boundary.ps1` catches mu-core regressions).
- Full preset `.muOn`; per-track `.muTrack`. Content dir `Documents/TDP/muOn` (`Presets/` + `Tracks/`).
- Engine params are APVTS floats/choices (automate + persist + bind to knobs); the step rows are ValueTree children of each track's node, not APVTS params. Naming: a layer is a "track", the sound type an "engine" (never "lane" / "channel" in new code).
- Audio thread never allocates: engine buffers in `prepare`, params via cached atomic pointers.

<!-- layer-extraction as-built 2026-10-17 -->
## Layer library (as built, E3)

- `Track : Layer` holds its engines as `TrackEngine`s (`Audio/TrackEngine.h`, a variant over Kick, Bass, `SampleChannel` for Hat and Snare, Rumble). An unlocked track holds all five prepared engines and plays the one `t{N}_eng` names, in the same block, from any thread. An `engineLocked` track (mu-Tronic) holds one.
- Each track clocks its own `StepRow` with a `StepClock` (`Sequencer/StepClock.h`, `StepRow.h`) and resolves params through its own `ModulationMatrix`. A Rumble is priority 1 and reads its source through `RenderContext::peer`. Everything renders through `renderAndMix`. There is no `GrooveVoices`; `GrooveSequencer` is test support under `Tests/`.
- `Persistence/TrackParams.{h,cpp}` holds the parameter table, the type mapping and `mu_on::remapRumbleSources`. `UI/TrackHost.h` is the UI host interface (`getTrackEngine`, `selectTrackEngine`).
- The Rumble reverb is pinned at RT60 1.8 s in `RumbleEngine::prepare`.
- Library: `mu-on-layer` (INTERFACE; `MU_ON_LAYER_UI_SOURCES`); tests `mu-on-tests` and `mu-on-layer-tests`. `engineLocked` defaults to false and is not saved. See `design-engine.md`.
