# mu-Toni — CLAUDE.md

Product-specific guidance for working in `mu-toni/`. Read alongside the family-wide [/CLAUDE.md](../CLAUDE.md).

## Project overview

**μ-Toni** is a JUCE/C++ **generative arpeggiator mono-synth** by Transwarp Development Project, and the family's first **freeware** product. Each voice is an independent monophonic arp: a note pool is derived from a **Scale × Chord × inversion × octave-span** (with optional diatonic snap), then scanned by a deterministic **skewed-triangle walk** (Direction −100 = down-arp, +100 = up-arp, 0 = up-down; the shorter sweep skips notes evenly to keep Rate timing uniform). The same parameters always produce the same sequence — the note is a pure function of the live parameter set, which is what makes every arp control a modulation destination.

The note drives a **note-triggered voice** ([Source/Audio/ToniVoice.h](Source/Audio/ToniVoice.h)): 2 oscillators + noise → mu-core `MultiModeFilter` → Amp / Filter / Pitch `juce::ADSR`, with portamento and legato, then the shared per-voice `InsertProcessor` post-VCA. Builds Standalone + VST3 + CLAP in the root build.

The editor inherits `mu-core/UI/EditorShellBase.h`, so TransportBar, StatusBar, About, overlays, window sizing and `MuLookAndFeel` match the rest of the family by construction.

## Current scope

### What's wired

- **`mu_toni::PluginProcessor : ProcessorBase`** ([Source/Plugin/PluginProcessor.{h,cpp}](Source/Plugin/PluginProcessor.cpp)) — 43-param/voice APVTS plus `mu_mixfx::addGlobalFxParams` + per-layer `ch{N}_` strips; renders through the shared `renderAndMix` driver (`Arp` layer → insert → `MixerEngine`); free-running internal transport driving the TransportBar.
- **Arp engine** — [Sequencer/ArpSequencer.h](Source/Sequencer/ArpSequencer.h) (pool derivation + skewed-triangle scan, pure and unit-tested) and [Sequencer/ArpVoiceRunner.h](Source/Sequencer/ArpVoiceRunner.h) (arp clock → voice note-on/off). Tables in [Audio/ToniScales.h](Source/Audio/ToniScales.h) and [Audio/Chords.h](Source/Audio/Chords.h) (~35 chords, absolute intervals, tier-grouped).
- **Voice** — [Audio/ToniVoice.h](Source/Audio/ToniVoice.h): mu-Tant's wavetable oscillators + 2-lane X-Mod + sync, shared from mu-core ([Audio/Wavetable/](../mu-core/Audio/Wavetable/) — `WavetableBank`, `XModOscPair`).
- **Per-voice modulation** — `Layer` (8 control sequences + `ModulationMatrix`) with the shared mu-core `ModulatorPanel` as the engine panel's bottom band; [Modulation/MuToniModDest.h](Source/Modulation/MuToniModDest.h) registers 20 arp/voice/env destinations, resolved per block through `mu_mod::resolveLane`. Persists in the APVTS state as the `MuToniMods` child.
- **`mu_toni::PluginEditor : EditorShellBase`** — shared shell + `LayerSidebar` + the shared `LayerPanel` around a populated [UI/ArpContent.h](Source/UI/ArpContent.h) (arp / osc / filter / env / insert, APVTS-attached shared widgets) + `MixerOverlay`. No bespoke shell or mixer code.
- **MIDI note input** — root note selectable by played note (latest note-on wins, held-note fallback stack); Loop vs MIDI-triggered run mode. μ-Toni is the first family member to consume played notes.
- **mu-link** — standalone bridge via the shared `MuLinkBridge.h` one-liner in [Source/Plugin/StandaloneApp.cpp](Source/Plugin/StandaloneApp.cpp), identity "mu-Toni"; arp clock slaves to the master clock when attached.
- **Standalone quit prompt** via the shared `mu_ui::confirmQuitAsync`.
- **1–8 layers** — a fresh instance has one; the sidebar Add button, header delete (with confirm) and drag-reorder go through the shared `ProcessorBase::addLayer` / `removeLayer` / `swapLayers` and `mu_ui::LayerEditController`. All eight layers' parameters (`a{N}_`, `ch{N}_`), `Arp`s and mixer strips exist from construction (`keepSpareLayers`); `processBlock` try-locks `layersLock` and renders the live layers only. The count and palette colours save as `numVoices` / `voiceColours` (not APVTS parameters); a state without them (saved when the count was fixed at four) loads as four layers. No cap (freeware).
- **`mu-toni-tests`** — arp engine, oscillator, and global-FX APVTS layout coverage; builds every default build.
- **Presets** — dirs + extensions wired: full = `.muToni`, per-slot = `.muArp`.

### Freeware — the License subsystem is unused by design

μ-Toni ships with **no purchase, licence key, activation, or demo mode**. The mu-core License subsystem (`LicenseManager`, `ActivationStore`, `OnlineActivation`, `LemonSqueezyClient`) and the shell's `ActivationOverlay` are deliberately not wired, `Source/License/` holds only `.gitkeep` (no `LicenseKey.h`, unlike mu-clid / mu-tant), and the About panel shows no licence status. This is a settled design decision, **not** an unfinished task — don't "fix" it by adding licence gating.

### Current placeholders / not yet wired

- **Preset save/load chrome** — the extensions and directories exist and loads go through the shared `ProcessorBase::layerSwapper` (stage while the resolved transport plays, commit on the next bar line; apply at once when stopped), but the browser chrome may still be disabled in the editor. DAW state via the APVTS works.
- **MIDI-PC preset hooks stubbed** — `applyMidiPresetLayer` / `applyFullMidiPreset` are empty overrides.
- **Oscillators are mu-Tant's wavetables** (procedural factory bank, no shipped content) with the same 2-lane X-Mod + sync, via the shared mu-core `XModOscPair`; param ids match mu-Tant's (`o1_wt`, `o1_pos`, `xmod_*`, `sync`).
- **UI is a functional grid** pending a `design-ui-family` polish pass.

### Conventions in force

- All product symbols under the `mu_toni::` namespace (boundary check `tests/scripts/check-core-boundary.ps1` catches mu-core regressions).
- `Source/` follows the family `{Plugin, Sequencer, UI, Persistence, License, Tests}` layout, **plus** `Audio/` and `Modulation/` for the engine and its mod-destination provider. `Persistence/` holds `ArpParams.{h,cpp}` and `LayerParamIds.h`; `License/` holds `LicenseKey.h`.
- Builds tick the shared family build counter (`add_dependencies(mu-toni increment_build_number)`).

## Design documents

| Doc | Covers |
|---|---|
| [docs/mu-toni/design.md](../docs/mu-toni/design.md) | Product overview, shared-engine inheritance map, file formats, freeware/distribution |
| [docs/mu-toni/design-sequencer.md](../docs/mu-toni/design-sequencer.md) | The arp model — scale/chord pool, skewed-triangle scan, gate/legato/portamento, MIDI control |
| [docs/mu-toni/design-buildplan.md](../docs/mu-toni/design-buildplan.md) | The staged T1–T5 MVP build plan |

<!-- layer-extraction as-built 2026-10-17 -->
## Layer library (as built, E2)

- Rendering goes through the shared `renderAndMix` driver. The layer type is `Arp` (`Sequencer/Arp.{h,cpp}`); held keys live in `Sequencer/HeldNotes.h`. The processor holds no per-voice glue.
- `ArpContent` takes `ProcessorBase&` and an `ArpHost&` (`UI/ArpHost.h`). Host methods carry the type name (`getArp...`).
- `Persistence/` holds `ArpParams.{h,cpp}` and `LayerParamIds.h` (moved from `Plugin/`). `Audio/ToniScales.h` was `Scales.h`.
- Library: `mu-toni-layer` (INTERFACE; UI sources in `MU_TONI_LAYER_UI_SOURCES`). Tests: `mu-toni-tests` and `mu-toni-layer-tests`. A library file never includes `Plugin/` or `License/` (`check-layer-boundary.ps1`).
