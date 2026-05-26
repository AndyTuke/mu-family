# μ-Clid

**Euclidean rhythm sequencer and sample trigger plugin** by Transwarp Development Project.

Available as VST3, CLAP, and Standalone for Windows.

---

## What it does

μ-Clid lets you create up to 8 independent polyrhythmic patterns using the Euclidean algorithm — a mathematical method for distributing hits as evenly as possible across a number of steps. Each rhythm slot has two hit generators (A and B) that can run at different lengths and offsets, producing complex interlocking grooves from simple controls.

Each slot plays back a sample through a full voice chain: pitch/filter/amp envelopes, a multi-mode insert effect (distortion, bitcrusher, flanger, echo), and sends to a shared Effect/Delay/Reverb bus. A built-in mixer gives you per-channel faders, pan, mute/solo, and sidechain ducking.

Patterns can be modulated by up to 8 per-rhythm LFO/step modulators. Everything saves and loads as a preset (`.muClid` files).

## Features

- Up to 8 polyrhythmic Euclidean rhythm slots
- Two hit generators per slot (independent step count, hits, rotation, offset)
- Sample playback with pitch, filter, and amp ADSR envelopes
- Insert effects: None, Soft/Hard/Fold distortion, Bitcrusher, Flanger, Echo
- Shared FX bus: Effect, Delay (tempo-sync or free), Reverb (Signalsmith FDN)
- Intra-FX routing (Effect → Delay → Reverb sends)
- Mixer with faders, pan, mute/solo, VU meters (Peak / VU / K-12 / K-14), and per-channel sidechain ducking
- Up to 8 LFO/step modulators per rhythm, with drawable shapes and bipolar/unipolar modes
- Drag-to-reorder rhythm slots
- Hot-swap a rhythm preset while playing (swaps at loop boundary)
- Preset system: full presets (`.muClid`) and per-rhythm presets (`.muRhythm`), with optional sample embedding

## Building

Requires a local [JUCE](https://juce.com) checkout. Set `JUCE_PATH` before configuring:

```powershell
$env:JUCE_PATH = "D:\JUCE"
cmake -B build
cmake --build build --config Release
```

Artefacts land in `build/mu-clid_artefacts/Release/`.

## Licence

Copyright © Transwarp Development Project. All rights reserved.
