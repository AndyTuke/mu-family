# μ-Toni — MVP build plan

Staged implementation of the design ([design.md](design.md) /
[design-sequencer.md](design-sequencer.md)). Goal: a **runnable MVP to start
adjusting** — an arpeggiator mono synth that makes sound and can be tweaked.

Owner gave a free hand on small calls; open decisions are taken with the best
default and noted in the docs.

## Stages

| # | Stage | Deliverable | State |
|---|---|---|---|
| **T1** | **Arp engine (pure, tested)** | `Audio/Scales.h`, `Audio/Chords.h`, `Sequencer/Arpeggiator.h` (header-only) — pool derivation (scale·chord·inversion·octaves) + skewed-triangle scan + step→MIDI. `Tests/ArpeggiatorTests.cpp`. | **✅ done — mu-toni-tests 0 failures/8 results** |
| **T2** | **Voice engine** | `Audio/AnalogueOsc.h` (PolyBLEP) + `Audio/ToniVoice.h` — 2 oscs + noise + `MultiModeFilter` + **Amp/Filter/Pitch `juce::ADSR`** + portamento slew + Legato. note-on/off + gate. `Tests/AnalogueOscTests.cpp`. | **✅ done** |
| **T3** | **Plugin integration** | `PluginProcessor` APVTS layout (43 params × voice), `Sequencer/ArpVoiceRunner.h` (arp clock → voice), render hook via `processCoreBlock`, transport-driven step clock, **MIDI note-in** (root + trigger), default patch. | **✅ done** |
| **T4** | **UI** | `EnginePanel` rebuilt as a rebindable arp/osc/filter/env control panel (shared `KnobWithLabel`/`DropdownSelect`/toggles, APVTS attachments), rebinds per sidebar layer. | **✅ done** |
| **T5** | **Presets · freeware · mu-link · polish** | `.muArp`/`.muToni` extensions; **freeware** (no licence wired = default); mu-link bridge one-liner in `StandaloneApp`; host/mu-link transport sync in `processBlock`; settings easter-egg line. | **✅ done** |

## Built — v1.0.943

All five stages compile + link across the family (mu-toni Standalone + VST3 + CLAP);
`mu-toni-tests` 0 failures / 10 results.

### MVP deviations from the design (follow-ups, logged)

- **Oscillator** — MVP shipped a self-contained analogue PolyBLEP osc; replaced (backlog
  #1073) by mu-Tant's wavetable engine, lifted to mu-core (`Audio/Wavetable/`: bank,
  oscillator, Hilbert, `XModOscPair`) so both products share mu-Tant's exact timbre.
- **X-Mod** — not in the MVP voice (osc detune only). Add with the wavetable lift.
- ~~Modulation~~ — **DONE (v1.0.945):** full modulation section wired identically
  to the other products. Per-voice `Layer` (8 control sequences + matrix), the
  shared `ModulatorPanel` as the engine panel's bottom band (rebinds per layer,
  playhead-driven), `Modulation/MuToniModDest.h` (20 arp/voice/env destinations),
  the engine resolves each voice's matrix via `mu_mod::resolveLane` each block
  (proportion space, depth scale 1.0), and modulators persist in the APVTS state
  (`MuToniMods` child). *Not unit-tested* (matrix needs mu-core linked into the
  test target — exercised at runtime instead).
- **Presets** — DAW state save/load works (APVTS); the `.muToni`/`.muArp` browser
  chrome + per-voice save are not yet wired.
- **UI polish** — functional grid layout; a design-ui-family pass (bands, sizes,
  BipolarSliderRow for Direction/Inversion) is a follow-up.

Each stage ends with a build. A **full family Debug build** (per the build rule)
happens at the first runnable checkpoint (end of T3) and each stage after.

## Key implementation notes

- **Namespace** `mu_toni::` for everything (boundary check enforces mu-core stays
  clean). Scales/Chords are copied into mu-toni (can't include mu-tant across
  products); centralise to mu-core later only if a sibling needs them.
- **Reuse** (no re-invention): `MultiModeFilter`, `InsertProcessor`,
  `VoiceParams`/`juce::ADSR`, `Layer`, `ModulationMatrix`, `MixerEngine`,
  `ProcessorBase::processCoreBlock`, all shared UI widgets, `MidiPresetMap`.
- **Arp = post-modulation reader**: the engine reads chord/root/direction/gate/
  ADSR values from the `ModulationMatrix` output, not raw APVTS (family rule).
- **Determinism**: the triangle scan is a pure index calc — identical every loop,
  no RNG.

## T1 engine API (this stage)

```cpp
namespace mu_toni {
  struct ArpParams { int scale, rootNote, rootOctave, chord, inversion,
                     octavesSpan; float direction; bool diatonicSnap; };

  // Pool of semitone offsets above the root (sorted asc). Returns count.
  int  buildPool (const ArpParams&, int* pool, int maxPool);
  // Skewed-triangle scan → pool-index sequence for one cycle. Returns cycle len.
  int  buildScan (int poolLen, float direction, int* scan, int maxScan);
  // Convenience: MIDI note for absolute step i.
  int  stepMidi  (const ArpParams&, int stepIndex);
}
```

Anchor tests: +100 = ascending run; −100 = descending; 0 = symmetric up-down;
+50 = full up + skipped-interior down; inversion ± shifts pool; octaves span
stacks; determinism (same params → same sequence twice).
