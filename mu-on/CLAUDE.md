# μ-On — CLAUDE.md

Product-specific guidance for working in `mu-on/`. Read alongside the family-wide [/CLAUDE.md](../CLAUDE.md).

## Project overview

**μ-On** is a JUCE/C++ **909-style groove sequencer** by Transwarp Development Project. Five
**fixed** instrument lanes — **Kick** (synthesis), **Bass** (deep synth — the focus), **Hat**
(sample), **Snare** (sample), and **Rumble** (a techno "rumble" that processes the Kick's audio
rather than stepping) — the first four sequenced on a 16-step grid. Builds Standalone + VST3 + CLAP
(+ AU on macOS via the family rule). Ships to the OneDrive tester share on Release (guarded by `MUFAMILY_DEPLOY_TESTERS`), alongside the rest of the family.

The editor inherits `mu-core/UI/EditorShellBase.h` and uses the shared `ChannelSidebar` +
`MixerOverlay`, so the TransportBar / StatusBar / About / window sizing / `MuLookAndFeel` and the
whole mixer/FX rack come for free. The product supplies the five engines, the 909 sequencer, and
the grid UI.

## Current scope

**Implemented**
- **Five fixed lanes** (`Channel` enum in [Source/Plugin/MuOnChannels.h](Source/Plugin/MuOnChannels.h): `kNumChannels=5`, `kNumStepLanes=4`) — no add/delete; the sidebar add/reorder hooks stay null. The first four step on the grid; **Rumble** (lane 4) has no steps — it processes the Kick's rendered audio.
- **Engines** ([Source/Audio/](Source/Audio/)) — `KickEngine` (synth), `BassEngine` (osc+sub+`MultiModeFilter`+drive), `SampleChannel` (Hat/Snare via mu-core `SamplePlayer`, procedurally-generated default one-shots), `RumbleEngine` (drive → tempo-synced delay taps → dark reverb → drawable per-bar volume envelope → final filter, fed the stashed kick render). `GrooveVoices` owns them, routes triggers + the mixer render hook, and each block resolves every lane's engine params **through that lane's `ModulationMatrix`** (proportion-space seed → matrix → write-back; try-lock, zero-cost when unused) before pushing them to the engines. See [docs/mu-on/design-engine.md](../docs/mu-on/design-engine.md).
- **909 sequencer** ([Source/Sequencer/](Source/Sequencer/)) — `StepPattern` (4×16, on/accent, atomic cells, `<Pattern>` persistence, default groove), `GrooveSequencer` (sample-accurate step-edge triggering off the internal beat, swing, accent velocity). See [docs/mu-on/design-sequencer.md](../docs/mu-on/design-sequencer.md).
- **Per-lane modulation** ([Source/Modulation/MuOnModDest.h](Source/Modulation/MuOnModDest.h)) — each lane owns a `VoiceSlot` (`ControlSequence` × N + `ModulationMatrix`); `MuOnModDest` is a **per-lane** destination provider (Kick/Bass/Hat/Snare expose different engine params, so the editor swaps the provider on lane select). Destination ids end in `.prop` → mu-core's `depthScaleFor` returns 1.0 (generic proportion-space rule), so a full-depth mod sweeps the whole knob. Modulators persist in a `<VoiceData>` child of the state tree (mirrors mu-tant).
- **Editor = family per-voice layout** (mirror mu-tant's `VoicePanel`): [GroovePanel](Source/UI/GroovePanel.h) stacks shared `ChannelHeaderBar` (lane name + Reset) → the selected lane's `EnginePanel` knobs → the **single-lane** `GrooveGrid` step editor (left=on / right=accent, playhead, Swing/Accent knobs — shows ONLY the selected lane) → the shared mu-core `ModulatorPanel` (rebinds to the lane's slot + dest provider; 30 Hz playhead). Only the engines + the step grid are product-specific; the rest is shared.
- **Bass↔Kick sidechain** — the shared `MixerEngine` channel-to-channel sidechain; the Bass channel's `ch1_scSrc` is pre-wired to Kick in the param layout. The Bass `EnginePanel` exposes **Duck / D.Atk / D.Rel** for smooth↔pumping.
- **Mixer/FX rack** — channel strips + global FX via `mu_mixfx::addGlobalFxParams`, synced through `ProcessorBase::syncGlobalFxParam` + a `parameterChanged` listener (mirrors mu-toni).
- **Internal free-running transport** drives the sequencer; **APVTS state** carries everything (`.muOn` full / `.muTrack` per-lane extensions wired).
- **Tests** ([Source/Tests/](Source/Tests/)) — ApvtsLayout, Sequencer (beat→step, firing, swing, accent, serialise), Engine (kick/hat sound + decay), Modulation (`.prop` scale, lane-scoped dest tables, modulator round-trip). Built every build (mirrors mu-tant/mu-toni-tests).

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
├── Audio/       KickEngine, BassEngine, SampleChannel, GrooveVoices (modulation routing)
├── Sequencer/   StepPattern, GrooveSequencer
├── Modulation/  MuOnModDest (per-lane modulation-destination provider)
├── UI/          GrooveGrid (single-lane step editor), GroovePanel (per-voice layout), EnginePanel
└── Tests/       TestMain + ApvtsLayout + Sequencer + Engine + Modulation
```

Mirrors the family `{Plugin, Audio, Sequencer, UI, Persistence, Tests}` topology (Persistence/License
land when preset I/O / licensing do).

## Design documents

| Sub-doc | When to read |
|---|---|
| [docs/mu-on/design-engine.md](../docs/mu-on/design-engine.md) | The five engines (incl. the Rumble lane), the bass deep-clean↔rumble range, the bass↔kick sidechain, RT-safety, the bass deepening roadmap. |
| [docs/mu-on/design-sequencer.md](../docs/mu-on/design-sequencer.md) | StepPattern model, clocking/swing/accent, the grid UI, future pattern chaining. |

## Conventions in force
- All product symbols under `mu_on::` (boundary check `tests/scripts/check-core-boundary.py` catches mu-core regressions).
- Full preset `.muOn`; per-lane `.muTrack`. Content dir `Documents/TDP/muOn` (`Presets/` + `Tracks/`).
- Engine params are APVTS floats/choices (automate + persist + bind to knobs); the step pattern is a ValueTree child, not APVTS params.
- Audio thread never allocates: engine buffers in `prepare`, params via cached atomic pointers.
