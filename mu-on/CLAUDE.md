# μ-On — CLAUDE.md

Product-specific guidance for working in `mu-on/`. Read alongside the family-wide [/CLAUDE.md](../CLAUDE.md).

## Project overview

**μ-On** is a JUCE/C++ **909-style groove sequencer** by Transwarp Development Project. Four
**fixed** instrument lanes — **Kick** (synthesis), **Bass** (deep synth — the focus), **Hat**
(sample), **Snare** (sample) — sequenced on a 16-step grid. Builds Standalone + VST3 + CLAP
(+ AU on macOS via the family rule). Local build only — not yet on the tester deploy.

The editor inherits `mu-core/UI/EditorShellBase.h` and uses the shared `ChannelSidebar` +
`MixerOverlay`, so the TransportBar / StatusBar / About / window sizing / `MuLookAndFeel` and the
whole mixer/FX rack come for free. The product supplies the four engines, the 909 sequencer, and
the grid UI.

## Current scope

**Implemented**
- **Four fixed lanes** (`Channel` enum in [Source/Plugin/MuOnChannels.h](Source/Plugin/MuOnChannels.h)) — no add/delete; the sidebar add/reorder hooks stay null.
- **Engines** ([Source/Audio/](Source/Audio/)) — `KickEngine` (synth), `BassEngine` (osc+sub+`MultiModeFilter`+drive), `SampleChannel` (Hat/Snare via mu-core `SamplePlayer`, procedurally-generated default one-shots). `GrooveVoices` owns them, routes triggers + the mixer render hook, pulls params from cached APVTS atomics. See [docs/mu-on/design-engine.md](../docs/mu-on/design-engine.md).
- **909 sequencer** ([Source/Sequencer/](Source/Sequencer/)) — `StepPattern` (4×16, on/accent, atomic cells, `<Pattern>` persistence, default groove), `GrooveSequencer` (sample-accurate step-edge triggering off the internal beat, swing, accent velocity). See [docs/mu-on/design-sequencer.md](../docs/mu-on/design-sequencer.md).
- **Grid UI** — `GrooveGrid` (left=on/right=accent, playhead, Swing/Accent knobs), `GroovePanel` (grid + per-lane `EnginePanel` knobs bound via APVTS attachments).
- **Bass↔Kick sidechain** — the shared `MixerEngine` channel-to-channel sidechain; the Bass channel's `ch1_scSrc` is pre-wired to Kick in the param layout. The Bass `EnginePanel` exposes **Duck / D.Atk / D.Rel** for smooth↔pumping.
- **Mixer/FX rack** — channel strips + global FX via `mu_mixfx::addGlobalFxParams`, synced through `ProcessorBase::syncGlobalFxParam` + a `parameterChanged` listener (mirrors mu-toni).
- **Internal free-running transport** drives the sequencer; **APVTS state** carries everything (`.muOn` full / `.muTrack` per-lane extensions wired).
- **Tests** ([Source/Tests/](Source/Tests/)) — ApvtsLayout, Sequencer (beat→step, firing, swing, accent, serialise), Engine (kick/hat sound + decay). Built every build (mirrors mu-tant/mu-toni-tests).

**Not yet implemented** (see the design docs' "Future" sections)
- Per-step **pitch** for the bass (unlocks glide + note-off → Release; these are intentionally not exposed yet so no inert controls are shown).
- **Real sample loading** ("Load .wav…") + shipped factory `.wav` (the `SampleChannel` buffer is the only seam; gated on code-signing like #99).
- Pattern **bank + chaining** (A/B + song mode), variable pattern length, sample-accurate onset.
- Preset save/load chrome (dirs + extensions are wired; the I/O body is deferred).

## Source layout

```
mu-on/Source/
├── Plugin/      PluginProcessor (ProcessorBase: APVTS, mixer/FX, sequencer, render dispatch),
│                PluginEditor (EditorShellBase), StandaloneApp (shared confirmQuitAsync), MuOnChannels.h
├── Audio/       KickEngine, BassEngine, SampleChannel, GrooveVoices
├── Sequencer/   StepPattern, GrooveSequencer
├── UI/          GrooveGrid, GroovePanel, EnginePanel
└── Tests/       TestMain + ApvtsLayout + Sequencer + Engine
```

Mirrors the family `{Plugin, Audio, Sequencer, UI, Persistence, Tests}` topology (Persistence/License
land when preset I/O / licensing do).

## Design documents

| Sub-doc | When to read |
|---|---|
| [docs/mu-on/design-engine.md](../docs/mu-on/design-engine.md) | The four engines, the bass deep-clean↔rumble range, the bass↔kick sidechain, RT-safety, the bass deepening roadmap. |
| [docs/mu-on/design-sequencer.md](../docs/mu-on/design-sequencer.md) | StepPattern model, clocking/swing/accent, the grid UI, future pattern chaining. |

## Conventions in force
- All product symbols under `mu_on::` (boundary check `tests/scripts/check-core-boundary.py` catches mu-core regressions).
- Full preset `.muOn`; per-lane `.muTrack`. Content dir `Documents/TDP/muOn` (`Presets/` + `Tracks/`).
- Engine params are APVTS floats/choices (automate + persist + bind to knobs); the step pattern is a ValueTree child, not APVTS params.
- Audio thread never allocates: engine buffers in `prepare`, params via cached atomic pointers.
