# mu-Tant — CLAUDE.md

Product-specific guidance for working in `mu-tant/`. Read alongside the family-wide [/CLAUDE.md](../CLAUDE.md).

## Project overview

**μ-Tant** is a JUCE/C++ wavetable drone synth by Transwarp Development Project. First-stab engine: a single free-running voice with two wavetable oscillators (cross-mod / FM / Sync), shared scale-quantised pitch, mu-core filter, and level. Standalone-only for now (no VST3/CLAP until it's worth shipping).

The editor inherits `mu-core/UI/EditorShellBase.h` — same TransportBar, About, StatusBar, window sizing, and `MuLookAndFeel` knob style as mu-Clid by construction. The differences vs mu-Clid are the voice engine and (eventually) the sequencer model.

## Current scope

**What's implemented**
- **8 voices** (`std::array<std::unique_ptr<VoiceEngine>, kMaxVoices>` in PluginProcessor) — each with 2 wavetable oscillators (`Source/Audio/SynthVoice.{h,cpp}`, `WavetableOscillator.h`, `WavetableBank.{h,cpp}`)
- 12-scale + 12-root tonal centre (`Source/Audio/Scales.h`), shared across voices
- mu-core multimode filter per voice
- Per-voice APVTS subtrees (`v{N}_*` for N=0..7) — osc1/osc2/xmod/mix/filter/level
- Per-channel mixer params (`ch{N}_lvl/pan/mute/solo`) + `mstr_lvl/pan`, read directly from APVTS in `processBlock`
- **Sidebar with 8 voice buttons** (`Source/UI/VoiceSidebar.{h,cpp}`) — radio-grouped, clicking rebinds `VoicePanel` to the selected voice
- Hand-laid `VoicePanel` with knob rows + per-voice rebind via `setVoice(int)`
- **Shared `MixerOverlay`** (`mu-core/UI/MixerOverlay.h`) — channel strips with level/pan/mute/solo are functional; FX-send + sidechain knobs render but are inert (their APVTS params aren't declared yet, see "MixerEngine refactor" below)
- **Per-voice modulator chain** — `std::array<VoiceSlot, kMaxVoices> voiceSlots` (each with 8 `ControlSequence` + `ModulationMatrix` + `modLock`); the shared mu-core `ModulatorPanel` is embedded in `VoicePanel`'s bottom band and rebinds to the active voice's slot. mu-tant destinations declared in [`Source/Modulation/MuTantModDest.h`](Source/Modulation/MuTantModDest.h) — 13 dests across Osc 1 / Osc 2 / Mix / Filter / Amp sections.
- Internal free-running beat counter (sample-rate-derived, 120 BPM default) drives modulator evaluation continuously (no transport needed)
- **`GatingDesigner` placeholder** (`Source/UI/GatingDesigner.{h,cpp}`) — full-width 2-bar grid strip with subdivision dropdown (1/4 / 1/8 / 1/16 default / 1/32). No gate data yet; this is the visual scaffold the drawable gate editor will sit inside.
- Shared editor shell + chrome (TransportBar, About, StatusBar, MuLookAndFeel)
- Unit tests on every build — currently 14 across DSP / modulator / gating subsystems

**What's not yet implemented**
- **Mixer FX rack**: the per-channel send knobs + return strips + Effect/Delay/Reverb rows render in the MixerOverlay but the APVTS params they bind to don't exist in mu-tant yet. The shared `MixerEngine::processBlock` also hard-codes `mu_core::VoiceEngine*` so mu-tant can't drive the shared FX/sidechain path until that signature accepts a render callback (see top-level backlog).
- Three-stacked-bands voice layout from design-voice.md (current layout is a single flat panel; design calls for Band 1 = Oscillators, Band 2 = Filter + Insert, Band 3 = Gate)
- Wavetable mip-mapping for anti-aliasing
- Linear cross-fade between adjacent wavetable frames at `position`
- Gate pattern data model (`GatePattern.{h,cpp}` in `Sequencer/`) and audio-thread evaluator
- Drawable gate-pattern editor with ALT-drag bend + per-envelope options (Reverse / Probability / Loop-N-of-M / First only / On staged-for-change only) — see [design-sequencer.md](../docs/mu-tant/design-sequencer.md)
- Properties strip below the gate grid
- Pattern hot-swap timing (reuse mu-clid's `HotSwapStager` machinery)
- Settings overlay (gear button is currently a no-op)
- Preset library (`.muPattern` per-voice / `.muTant` full) — extensions declared but no save/load code
- VST3 / CLAP formats + tester deploy (Standalone-only for now)

## Build targets

- `mu-tant` — Standalone synth (Debug-only deploy locally; not shipped to testers)
- `mu-tant-tests` — DSP UnitTest runner; **builds as part of `ALL_BUILD`** (dropped EXCLUDE_FROM_ALL — every mu-tant build runs the tests)

## Source layout

```
mu-tant/Source/
├── Plugin/           PluginProcessor, PluginEditor (extends EditorShellBase)
├── Sequencer/        (empty — GatePattern.{h,cpp} lands when the drawable editor goes in)
├── UI/               VoicePanel + VoiceSidebar + GatingDesigner
├── Audio/            SynthVoice (multi-voice), WavetableOscillator, WavetableBank, Scales
├── Modulation/       MuTantModDest (destination provider for the shared ModulatorPanel)
├── Persistence/      (empty — preset I/O not yet implemented)
├── License/          (empty — no licensing yet)
└── Tests/            TestMain + SynthDSPTests + ModulatorTests + GatingDesignerTests
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
