# mu-Toni — CLAUDE.md

Product-specific guidance for working in `mu-toni/`. Read alongside the family-wide [/CLAUDE.md](../CLAUDE.md).

## Project overview

**μ-Toni** is a JUCE/C++ **generative arpeggiator mono-synth** by Transwarp Development Project, and the family's first **freeware** product. Each voice is an independent monophonic arp: a note pool is derived from a **Scale × Chord × inversion × octave-span** (with optional diatonic snap), then scanned by a deterministic **skewed-triangle walk** (Direction −100 = down-arp, +100 = up-arp, 0 = up-down; the shorter sweep skips notes evenly to keep Rate timing uniform). The same parameters always produce the same sequence — the note is a pure function of the live parameter set, which is what makes every arp control a modulation destination.

The note drives a **note-triggered voice** ([Source/Audio/ToniVoice.h](Source/Audio/ToniVoice.h)): 2 oscillators + noise → mu-core `MultiModeFilter` → Amp / Filter / Pitch `juce::ADSR`, with portamento and legato, then the shared per-voice `InsertProcessor` post-VCA. Builds Standalone + VST3 + CLAP in the root build.

The editor inherits `mu-core/UI/EditorShellBase.h`, so TransportBar, StatusBar, About, overlays, window sizing and `MuLookAndFeel` match the rest of the family by construction.

## Current scope

### What's wired

- **`mu_toni::PluginProcessor : ProcessorBase`** ([Source/Plugin/PluginProcessor.{h,cpp}](Source/Plugin/PluginProcessor.cpp)) — 43-param/voice APVTS plus `mu_mixfx::addGlobalFxParams` + per-layer `ch{N}_` strips; renders through the shared `MixerEngine` (engine → insert → mixer) via the `processCoreBlock` hook; free-running internal transport driving the TransportBar.
- **Arp engine** — [Sequencer/Arpeggiator.h](Source/Sequencer/Arpeggiator.h) (pool derivation + skewed-triangle scan, pure and unit-tested) and [Sequencer/ArpVoiceRunner.h](Source/Sequencer/ArpVoiceRunner.h) (arp clock → voice note-on/off). Tables in [Audio/Scales.h](Source/Audio/Scales.h) and [Audio/Chords.h](Source/Audio/Chords.h) (~35 chords, absolute intervals, tier-grouped).
- **Voice** — [Audio/ToniVoice.h](Source/Audio/ToniVoice.h): mu-Tant's wavetable oscillators + 2-lane X-Mod + sync, shared from mu-core ([Audio/Wavetable/](../mu-core/Audio/Wavetable/) — `WavetableBank`, `XModOscPair`).
- **Per-voice modulation** — `VoiceSlot` (8 control sequences + `ModulationMatrix`) with the shared mu-core `ModulatorPanel` as the engine panel's bottom band; [Modulation/MuToniModDest.h](Source/Modulation/MuToniModDest.h) registers 20 arp/voice/env destinations, resolved per block through `mu_mod::resolveLane`. Persists in the APVTS state as the `MuToniMods` child.
- **`mu_toni::PluginEditor : EditorShellBase`** — shared shell + `ChannelSidebar` + a populated [UI/EnginePanel.h](Source/UI/EnginePanel.h) (arp / osc / filter / env / insert, APVTS-attached shared widgets) + `MixerOverlay`. No bespoke shell or mixer code.
- **MIDI note input** — root note selectable by played note (latest note-on wins, held-note fallback stack); Loop vs MIDI-triggered run mode. μ-Toni is the first family member to consume played notes.
- **mu-link** — standalone bridge via the shared `MuLinkBridge.h` one-liner in [Source/Plugin/StandaloneApp.cpp](Source/Plugin/StandaloneApp.cpp), identity "mu-Toni"; arp clock slaves to the master clock when attached.
- **Standalone quit prompt** via the shared `mu_ui::confirmQuitAsync`.
- **`mu-toni-tests`** — arp engine, oscillator, and global-FX APVTS layout coverage; builds every default build.
- **Presets** — dirs + extensions wired: full = `.muToni`, per-slot = `.muArp`.

### Freeware — the License subsystem is unused by design

μ-Toni ships with **no purchase, licence key, activation, or demo mode**. The mu-core License subsystem (`LicenseManager`, `ActivationStore`, `OnlineActivation`, `LemonSqueezyClient`) and the shell's `ActivationPanel` are deliberately not wired, `Source/License/` holds only `.gitkeep` (no `LicenseKey.h`, unlike mu-clid / mu-tant), and the About panel shows no licence status. This is a settled design decision, **not** an unfinished task — don't "fix" it by adding licence gating.

### Current placeholders / not yet wired

- **Fixed 4 "Layer" channels** — `kNumChannels = 4` of `kMaxChannels = 8` ([PluginProcessor.h:35-36](Source/Plugin/PluginProcessor.h#L35)). Dynamic add/delete/reorder is unwired (no `addVoice`/`removeVoice`), so **the sidebar Add button is inert**; the design calls for 1–8 independent mono arps, default 1.
- **Preset save/load chrome** — the extensions and directories exist, but save/load stay on `ProcessorBase`'s no-op defaults and the browser chrome is disabled in the editor. DAW state via the APVTS works.
- **MIDI-PC preset hooks stubbed** — `applyMidiPresetSlot` / `applyFullMidiPreset` are empty overrides.
- **Oscillators are mu-Tant's wavetables** (procedural factory bank, no shipped content) with the same 2-lane X-Mod + sync, via the shared mu-core `XModOscPair`; param ids match mu-Tant's (`o1_wt`, `o1_pos`, `xmod_*`, `sync`).
- **UI is a functional grid** pending a `design-ui-family` polish pass.

### Conventions in force

- All product symbols under the `mu_toni::` namespace (boundary check `tests/scripts/check-core-boundary.py` catches mu-core regressions).
- `Source/` follows the family `{Plugin, Sequencer, UI, Persistence, License, Tests}` layout, **plus** `Audio/` and `Modulation/` for the engine and its mod-destination provider. `Persistence/` and `License/` are currently placeholders (`.gitkeep` only).
- Builds tick the shared family build counter (`add_dependencies(mu-toni increment_build_number)`).

## Design documents

| Doc | Covers |
|---|---|
| [docs/mu-toni/design.md](../docs/mu-toni/design.md) | Product overview, shared-engine inheritance map, file formats, freeware/distribution |
| [docs/mu-toni/design-sequencer.md](../docs/mu-toni/design-sequencer.md) | The arp model — scale/chord pool, skewed-triangle scan, gate/legato/portamento, MIDI control |
| [docs/mu-toni/design-buildplan.md](../docs/mu-toni/design-buildplan.md) | The staged T1–T5 MVP build plan |
