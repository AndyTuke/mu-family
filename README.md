# μ-series — Transwarp Development Project

Audio plugins and tools for Windows (macOS coming soon).

| Product | Description | Format |
|---|---|---|
| **μ-Clid** | Euclidean rhythm sequencer + sample trigger | VST3 · CLAP · Standalone |
| **μ-Clid Lite** | Single-track euclidean MIDI sequencer — free | VST3 · CLAP |
| **μ-Tant** | Wavetable drone synth with envelope gate sequencer | VST3 · CLAP · Standalone |
| **μ-link** | BPM + transport sync hub for the μ-series standalone apps | Standalone |

Website: [transwarp.me](https://transwarp.me)

---

## μ-Clid

Up to 8 independent polyrhythmic Euclidean rhythm slots, each with two hit generators (independent step count, hits, rotation, offset) combined through boolean logic gates (AND / OR / XOR / A Only / B Only). Each slot plays a sample through a full voice chain — pitch, filter, amp ADSR envelopes, insert effect — with sends to a shared Effect / Delay / Reverb bus. Up to 8 LFO/step modulators per rhythm. Hot-swap presets while playing.

**μ-Clid Lite** is a free, permanently unlicensed single-track variant: one rhythm slot, full euclidean engine, MIDI note output only (no sample engine), save enabled, no time limit.

## μ-Tant

A permanent dual-oscillator wavetable drone shaped by FM and ring modulation (x-mod) and sculpted rhythmically by an envelope gate sequencer of up to 16 bars. Each step in the gate sequencer has its own envelope, per-step filter cutoff and pitch, probability, and loop-N-of-M gating. Up to 8 voices, each with its own modulator chain. Routes through the same mixer as μ-Clid (three sends, two master inserts, per-channel sidechain).

## μ-link

A lightweight standalone sync hub. Connects μ-Clid and μ-Tant standalone apps to a shared BPM clock and transport. No plugin format — standalone only.

---

## Repository layout

```
mu-core/        Shared audio engine, FX, modulation, mixer UI (INTERFACE library)
mu-clid/        μ-Clid + μ-Clid Lite source
mu-tant/        μ-Tant source
mu-link/        μ-link sync hub
mu-toni/        Scaffolding (not yet in development)
mu-on/          Scaffolding (not yet in development)
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
