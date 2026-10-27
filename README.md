# μ-series — Transwarp Development Project

Audio plugins and tools for Windows (Linux in beta; macOS on hold).

| Product | Description | Format |
|---|---|---|
| **μ-Clid** | Euclidean rhythm sequencer + sample trigger | VST3 · CLAP · Standalone |
| **μ-Clid MIDI** | Single-rhythm euclidean MIDI effect (the former μ-Clid Lite) — free | VST3 · CLAP |
| **μ-Tant** | Wavetable drone synth with envelope gate sequencer | VST3 · CLAP · Standalone |
| **μ-Toni** | Generative arpeggiator mono-synth | VST3 · CLAP · Standalone |
| **μ-On** | 909-style groove sequencer with Kick, Bass, Hat, Snare and Rumble tracks | VST3 · CLAP · Standalone |
| **μ-Tronic** | Performance unit: 16 layer slots holding layers from the other products, with buses and scenes | Standalone |
| **μ-link** | BPM + transport sync hub for the μ-series standalone apps | Standalone |

Website: [transwarp.me](https://transwarp.me)

---

## μ-Clid

Up to 8 independent polyrhythmic Euclidean rhythm slots, each with two hit generators (independent step count, hits, rotation, offset) combined through boolean logic gates (AND / OR / XOR / A Only / B Only). Each slot plays a sample through a full voice chain — pitch, filter, amp ADSR envelopes, insert effect — with sends to a shared Effect / Delay / Reverb bus. Up to 8 LFO/step modulators per rhythm. Hot-swap presets while playing.

**μ-Clid MIDI** is a separate, free product (the former μ-Clid Lite): one rhythm, the full euclidean engine, MIDI note output only (no sample engine). It shares the rhythm preset format with μ-Clid.

## μ-Tant

A permanent dual-oscillator wavetable drone shaped by FM and ring modulation (x-mod) and sculpted rhythmically by an envelope gate sequencer of up to 16 bars. Each step in the gate sequencer has its own envelope, with per-step filter cutoff and pitch. Up to 8 voices, each with its own modulator chain. Routes through the same mixer as μ-Clid (three sends, two master inserts, per-channel sidechain).

## μ-Toni

A generative arpeggiator mono-synth. A note pool is derived from a scale, a chord, an inversion and an octave span, then walked by a deterministic skewed-triangle scan, so the same settings always give the same sequence and every arp control is a modulation destination. Each layer is an independent monophonic voice (two wavetable oscillators with X-Mod, filter, amp / filter / pitch envelopes, insert effect); up to 8 layers share the mixer.

## μ-On

A 909-style groove sequencer. Up to 8 tracks, each running one engine: Kick (synth), Bass (deep synth), Hat and Snare (sample), or Rumble (a techno rumble that processes another track's audio). Each track has a 16-step row with accents and swing, its own modulators and insert effect, and the shared mixer with channel-to-channel sidechain.

## μ-Tronic

A performance unit for the whole series. 16 layer slots (paged 1–8 / 9–16), each holding a layer taken from another product: μ-Clid rhythms, μ-Tant patterns, μ-Toni arps and every μ-On engine. It adds three group buses with two inserts each, scenes that launch clip *n* on every layer at once, and the same modulators, insert effects and mixer as the rest of the series. Standalone only.

## μ-link

A lightweight standalone sync hub. Connects the μ-series standalone apps to a shared BPM clock and transport. No plugin format — standalone only.

---

## Repository layout

```
mu-core/        Shared audio engine, FX, modulation, mixer UI (INTERFACE library)
mu-clid/        μ-Clid source
mu-clid-midi/   μ-Clid MIDI source — the Euclidean MIDI effect
mu-tant/        μ-Tant source
mu-link/        μ-link sync hub
mu-on/          μ-On source — 909-style groove sequencer
mu-toni/        μ-Toni source — generative arpeggiator mono-synth
mu-tronic/      μ-Tronic source — 16-slot performance unit (Standalone only)
mu-control/     Hardware controller drivers and surface model (depends on mu-core)
docs/           Design documents
site/           Marketing website (deployed to transwarp.me via Netlify)
tests/          Cross-plugin listening-test pipeline
tools/          License signing tool (MuLicenseTool)
cmake/          Shared CMake scripts
ThirdParty/     JUCE extensions, Signalsmith DSP, Monocypher, CLAP
```

---

## Building

Requires a local [JUCE](https://juce.com) checkout. Set `JUCE_PATH` before configuring:

```powershell
$env:JUCE_PATH = "D:\JUCE"
cmake -B build
cmake --build build --config Debug
cmake --build build --config Release
```

Release artifacts land in `build/<product>/<target>_artefacts/Release/`. Release builds also produce customer zips in `build/dist/`.

---

## Licence

Copyright © Transwarp Development Project. All rights reserved.

The source code in this repository is made available for reference. You may read and study it, but you may not distribute modified or unmodified copies, sell compiled binaries, or use it as the basis for a commercial product without written permission.
