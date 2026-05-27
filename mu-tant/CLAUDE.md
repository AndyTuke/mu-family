# mu-Tant — CLAUDE.md

Product-specific guidance for working in `mu-tant/`. Read alongside the family-wide [/CLAUDE.md](../CLAUDE.md).

## Project overview

**μ-Tant** is a JUCE/C++ wavetable drone synth by Transwarp Development Project. First-stab engine: a single free-running voice with two wavetable oscillators (cross-mod / FM / Sync), shared scale-quantised pitch, mu-core filter, and level. Standalone-only for now (no VST3/CLAP until it's worth shipping).

The editor inherits `mu-core/UI/EditorShellBase.h` — same TransportBar, About, StatusBar, window sizing, and `MuLookAndFeel` knob style as mu-Clid by construction. The differences vs mu-Clid are the voice engine and (eventually) the sequencer model.

## Current scope (first stab)

**What's implemented**
- 1 voice with 2 wavetable oscillators (`Source/Audio/SynthVoice.{h,cpp}`, `WavetableOscillator.h`, `WavetableBank.{h,cpp}`)
- 12-scale + 12-root tonal centre (`Source/Audio/Scales.h`)
- mu-core multimode filter on the voice output
- APVTS-driven parameters wired to a hand-laid `VoicePanel` of `KnobWithLabel` + `DropdownSelect` controls
- Shared editor shell + chrome (TransportBar, About, StatusBar, MuLookAndFeel)
- DSP unit tests on Scales + WavetableOscillator + WavetableBank, running on every build

**What's not yet implemented**
- Sequencer / gate / pattern editor
- Multi-voice (target: 8 voices, sidebar with per-voice edit button)
- Mixer overlay
- Per-voice modulator chain + ModulationMatrix wiring
- Settings overlay (gear button is currently a no-op)
- Preset library (`.muPattern` per-voice / `.muTant` full)
- VST3 / CLAP formats + tester deploy

## Build targets

- `mu-tant` — Standalone synth (Debug-only deploy locally; not shipped to testers)
- `mu-tant-tests` — DSP UnitTest runner; **builds as part of `ALL_BUILD`** (dropped EXCLUDE_FROM_ALL — every mu-tant build runs the tests)

## Source layout

```
mu-tant/Source/
├── Plugin/           PluginProcessor, PluginEditor (extends EditorShellBase)
├── Sequencer/        (empty — sequencer not yet implemented)
├── UI/               VoicePanel (single-voice param page)
├── Audio/            SynthVoice, WavetableOscillator, WavetableBank, Scales
├── Persistence/      (empty — preset I/O not yet implemented)
├── License/          (empty — no licensing yet)
└── Tests/            TestMain + SynthDSPTests
```

When new subsystems land (mixer, modulator, sequencer), they go under the same `{Audio, Sequencer, UI, Persistence}` topology as mu-Clid per the family consistency rule.

## Design documents

| Sub-doc | When to read |
|---|---|
| [docs/mu-tant/design-voice.md](../docs/mu-tant/design-voice.md) | Voice chain, oscillators, scale-quantised pitch, filter |
| [docs/mu-tant/design-sequencer.md](../docs/mu-tant/design-sequencer.md) | Gate / pattern editor design |

Add new product docs at `docs/mu-tant/` mirroring `docs/mu-clid/` as topics get decided.

## File extensions (forward-looking)

Per the family consistency rule: per-layer preset = `.muPattern` (camelCase noun); full preset = `.muTant` (plugin-name camelCase). These are declared in `PluginProcessor::getPerSlotPresetExtension()` + `getFullPresetExtension()` but no preset I/O code consumes them yet.
