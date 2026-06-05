# mu-Tant — CLAUDE.md

Product-specific guidance for working in `mu-tant/`. Read alongside the family-wide [/CLAUDE.md](../CLAUDE.md).

## Project overview

**μ-Tant** is a JUCE/C++ wavetable drone synth by Transwarp Development Project. Engine: a dynamic set of **1–8 free-running voices** (default 1), each with two wavetable oscillators (cross-mod / FM / Sync), shared scale-quantised pitch, mu-core filter, a per-voice drawable **gater**, and a shared insert effect. Builds Standalone + VST3 + CLAP (Debug + Release; Release deploys to the OneDrive tester share via `MUFAMILY_DEPLOY_TESTERS=ON`, matching mu-clid).

The editor inherits `mu-core/UI/EditorShellBase.h` — same TransportBar, About, StatusBar, window sizing, and `MuLookAndFeel` knob style as mu-Clid by construction. The differences vs mu-Clid are the voice engine and (eventually) the sequencer model.

## Current scope

**What's implemented**
- **Dynamic 1–8 voices** (default 1) — `PluginProcessor` tracks `numVoices` with `addVoice`/`removeVoice`/`swapVoices`/`resetVoice` (message-thread, guarded by `voicesLock`; `processBlock` tryLocks). Each voice has 2 wavetable oscillators (`Source/Audio/SynthVoice.{h,cpp}`, `WavetableOscillator.h`, `WavetableBank.{h,cpp}`)
- 12-scale + 12-root tonal centre (`Source/Audio/Scales.h`), shared across voices
- mu-core multimode filter per voice
- Per-voice APVTS subtrees (`v{N}_*` for N=0..7) — osc1/osc2/xmod/levels/filter/level/gate/insert; per-voice palette colour allocated on add and following the voice through delete/reorder
- Per-channel mixer params (`ch{N}_lvl/pan/mute/solo`) + `mstr_lvl/pan`, read via cached APVTS atomic pointers (no per-block string building)
- **Shared left sidebar** — `VoiceSidebar` is a thin subclass of mu-core's `ChannelSidebar` (`Source/UI/VoiceSidebar.{h,cpp}`): select / **add** / **delete** / drag-**reorder**, with a per-voice glyph mini-graphic
- Hand-laid `VoicePanel` with per-voice rebind via `setVoice(int)` (which repaints so borders/titles track the active voice's palette colour). Layout: per-oscillator sub-panels (wavetable selector + Oct/Semi/Fine/Pos), an X-Mod/Noise row, a Filter row (Type + Cutoff + Resonance + per-voice Level), the shared **`ChannelHeaderBar`** at top, the shared **`InsertSubsection`** (INSERT sub-panel, `"v"` prefix), the gate editor, and a right-hand **Mixer sub-panel** (Osc 1 / Osc 2 / Noise levels). Knob colours follow mu-clid's category logic; osc/mixer titles take the per-voice palette colour.
- **Shared per-voice insert effect** — each voice runs mu-core's `InsertProcessor` post-gate (engine → insert → mixer); selectable via the shared `InsertSubsection` UI (`v{N}_drvChar` + `v{N}_insP1..4`)
- **Routes through the shared `MixerEngine`** — `processBlock` calls `ProcessorBase::processCoreBlock(..., &renderVoiceCb)`; the render hook fills each channel (modulation → engine → gate → insert) and the shared mixer owns the strip/master mix, so the MixerOverlay VU meters are live
- **Full mixer FX rack** — channel sends + sidechain + the shared Effect/Delay/Reverb slots + FX returns + master inserts are declared via `mu_mixfx::addGlobalFxParams` (mu-core) and synced to `fxChain`/`mixerEngine` through `ProcessorBase::syncGlobalFxParam`, driven by a `parameterChanged` listener (channel/return/master state kept in sync; an initial + post-preset full sync seeds it). `fxChain.prepare` runs in `prepareToPlay`
- **Confirmation dialogs** — shared `mu-core/UI/ConfirmDialog.h`: reset/delete a voice prompts first (`mu_ui::confirmAsync`); closing the standalone shows the "Close μ-Tant?" prompt (`mu_ui::confirmQuitAsync` via `Source/Plugin/StandaloneApp.cpp`)
- **Per-voice modulator chain** — `std::array<VoiceSlot, kMaxVoices> voiceSlots` (each with 8 `ControlSequence` + `ModulationMatrix` + `modLock`); the shared mu-core `ModulatorPanel` is embedded in `VoicePanel`'s bottom band and rebinds to the active voice's slot. mu-tant destinations declared in [`Source/Modulation/MuTantModDest.h`](Source/Modulation/MuTantModDest.h)
- Internal free-running transport (`internalBpm` atomic, 120 BPM default) drives modulator evaluation + the gate + the gating-grid playhead while playing
- **Drawable gate editor** (`Source/UI/GatingDesigner.{h,cpp}` + `Source/Sequencer/GatePattern.{h,cpp}`) — full-width 2-bar grid with subdivision dropdown (1/4 / 1/8 / 1/16 default / 1/32). Each envelope has an **attack/decay shape** (`startCell` + `lengthCells` + `split` + `attackBend` / `decayBend` + `reverse`) plus per-envelope **Probability** (0–100 %) and **Loop-N-of-M** (fires on loop N of every M-loop cycle). Toolbox tools: **Arrow** (select + properties), **Pencil**, **Eraser**, **Glue**, **Reverse**. Per-voice **Gap** knob + **Gater bypass** toggle. **Filter envelope layer** (toggle [GATE|FILT]) shapes filter cutoff per-voice (block-accurate; 0 = 20 Hz, 1 = base cutoff); ghost of inactive layer drawn at 20% alpha. **Properties strip** below grid shows Prob slider + Loop N/M dropdowns for the selected envelope. Audio: `applyGateBlock()` shapes post-filter amplitude; filter layer applied in `renderVoice` before `setConfig`. Pattern loop count tracked via `loopCount` atomic. See [docs/mu-tant/design-sequencer.md](../docs/mu-tant/design-sequencer.md).
- **Full preset system** — `getContentDir()` = `Documents/TDP/muTant`; full presets (`.muTant`) save/load the whole APVTS state (name/description/category) in `Presets/`, driven by the shared shell chrome
- **Per-voice presets** (`.muPattern`, in `Voices/`) — save/load one voice's `v{N}_*` params via the shared `ChannelHeaderBar` Save/dropdown
- **Modulator + gate + filter gate persistence** — each voice's `ControlSequence`s + `ModulationMatrix` + gate envelopes + filter envelopes serialised into `<VoiceData>` child of the APVTS state for full + `.muTant` presets, and into each `.muPattern`. New `probability`/`loopN`/`loopM` envelope fields serialised alongside the existing shape fields.
- **Preset hot-swap** — full presets (`loadPreset`) and per-voice `.muPattern` (`loadVoicePreset`) stage while the transport plays and commit at a loop boundary (voice 0's gate pattern for a full preset; the voice's own for a per-voice swap), so a switch is musically seamless; stopped → applies immediately. Product-side `Source/Plugin/{HotSwapBoundary,VoiceHotSwapStager}.h` (the mu-clid `HotSwapStager` is deliberately NOT centralised to mu-core — payload + boundary semantics are product-specific). `PluginProcessor` is a `juce::AsyncUpdater`; commits fire `onPresetSwapCommitted` / `onVoiceHotSwapCommitted` for the editor refresh
- **MIDI program change → preset** (same model as mu-clid) — Ch 1-8 hot-swap the matching voice's `.muPattern`, Ch 9 hot-swaps the full `.muTant` preset, via the shared mu-core `MidiPresetMap` / `MidiFullPresetMap` + `scanMidiProgramChanges`. Configured from the SettingsOverlay "MIDI Prog. Change" row (Voice / Full Presets buttons → the shared `MidiPresetsPanel` / `MidiFullPresetsPanel`)
- **Settings overlay** (`Source/UI/SettingsOverlay.{h,cpp}`) behind the gear button — master vol + UI size + BPM + MIDI program-change tables
- Shared editor shell + chrome (TransportBar, About, StatusBar, MuLookAndFeel)
- Unit tests on every build — across DSP / modulator / gating / insert subsystems

**What's not yet implemented**
- Wavetable mip-mapping for anti-aliasing
- Linear cross-fade between adjacent wavetable frames at `position`
- Per-envelope Probability + Loop-N-of-M are **implemented** (Arrow tool + properties strip). Remaining: "First-only" and "On-staged-for-change" are still deferred.

## Build targets

- `mu-tant` — wavetable drone synth, **Standalone + VST3 + CLAP** (VST3 via `FORMATS`, CLAP via `clap_juce_extensions_plugin`; built local, not shipped to testers)
- `mu-tant-tests` — DSP UnitTest runner; **builds as part of `ALL_BUILD`** (dropped EXCLUDE_FROM_ALL — every mu-tant build builds the tests). Intentionally differs from `mu-clid-tests` (`EXCLUDE_FROM_ALL`): mu-tant is small + Debug-only/local so building its tests every build is cheap, whereas mu-clid's heavy suite would slow its tester-deploy Release build. Documented as a deliberate split in [mu-clid/CLAUDE.md](../mu-clid/CLAUDE.md).

## Source layout

```
mu-tant/Source/
├── Plugin/           PluginProcessor (dynamic voices + preset/modulator/gate I/O), PluginEditor (extends EditorShellBase)
├── Sequencer/        GatePattern (drawable gate model + shared block gater)
├── UI/               VoicePanel + VoiceSidebar (ChannelSidebar subclass) + GatingDesigner + SettingsOverlay
├── Audio/            SynthVoice (multi-voice), WavetableOscillator, WavetableBank, Scales
├── Modulation/       MuTantModDest (destination provider for the shared ModulatorPanel)
├── Persistence/      (empty — preset/modulator/gate I/O lives in PluginProcessor + mu-core's ModulatorSerialise)
├── License/          (empty — no licensing yet)
└── Tests/            TestMain + SynthDSPTests + ModulatorTests + GatingDesignerTests + GatePatternTests + GateStageTests + InsertStageTests
```

When new subsystems land they go under the same `{Audio, Sequencer, UI, Persistence}` topology as mu-Clid per the family consistency rule.

## Design documents

| Sub-doc | When to read |
|---|---|
| [docs/mu-tant/design-voice.md](../docs/mu-tant/design-voice.md) | Voice chain, oscillators, scale-quantised pitch, filter |
| [docs/mu-tant/design-sequencer.md](../docs/mu-tant/design-sequencer.md) | Gate / pattern editor design |
| [docs/mu-tant/create_manual.ps1](../docs/mu-tant/create_manual.ps1) | **End-user manual source** — generates `mu-Tant User Manual.docx` via Word automation (mirrors mu-clid). Edit this script (not the `.docx`), then re-run it. |

Add new product docs at `docs/mu-tant/` mirroring `docs/mu-clid/` as topics get decided.

## File extensions (forward-looking)

Per the family consistency rule: per-layer preset = `.muPattern` (camelCase noun, in `Voices/`); full preset = `.muTant` (plugin-name camelCase, in `Presets/`). Both are wired: `saveVoicePreset`/`loadVoicePreset` (per-voice) and `savePreset`/`loadPreset` (full), declared via `getPerSlotPresetExtension()` + `getFullPresetExtension()`.
