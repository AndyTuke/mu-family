# mu-Clid MIDI — CLAUDE.md

Product-specific guidance for working in `mu-clid-midi/`. Read alongside the family-wide [/CLAUDE.md](../CLAUDE.md).

## Project overview

**μ-Clid MIDI** is a single-rhythm Euclidean **MIDI effect** (VST3 + CLAP, no audio, no Standalone) by Transwarp Development Project. It is the former mu-Clid Lite, now its own free product: no licensing, no support prompt. The ruling and the staged plan are in [docs/design-plugin-family.md](../docs/design-plugin-family.md) ("The MIDI-effect product"); product notes are in [docs/mu-clid-midi/](../docs/mu-clid-midi/README.md).

## Build targets

- `mu-clid-midi` — the plugin (VST3 + CLAP; AU on macOS only, which is on hold)
- `mu-clid-midi-tests` — console UnitTest runner, **`EXCLUDE_FROM_ALL`** like `mu-clid-tests`; build it with `cmake --build build --target mu-clid-midi-tests`

Artefacts are `mu-Clid MIDI.vst3` / `.clap` in `build/mu-clid-midi/mu-clid-midi_artefacts/<Config>/`. The Release installer is `installer/mu-Clid-MIDI.iss` (built by `cmake/BuildInstallerMidi.cmake`).

## Plugin IDs never change

`PLUGIN_CODE MCLt`, manufacturer `TDP1`, `BUNDLE_ID` and `CLAP_ID` `com.transwarp.mu-clid-lite`, the processor state id `MuClidState`, and the parameter ids (`r0_*`, `mstrLoop`, `lite_midiNote`, `lite_accentAmt`) are saved data: DAW projects find the plugin and its settings by them. Only the display name is new. The processor reads sessions saved by mu-Clid Lite (a full mu-Clid preset tree) as well as its own (`Persistence/SessionState`).

## Source layout

```
mu-clid-midi/Source/
├── Plugin/        PluginProcessor (host glue: transport, state), PluginEditor, ParameterLayout.h
├── Persistence/   SessionState (host session text, plus the Lite-era session reader)
├── Sequencer/     MidiRhythmCore (the audio path: rhythm, note engine, parameter sync, rhythm lock; no AudioProcessor), AccentVelocity.h
├── Tests/         juce::UnitTest suite (layout, session state, accent velocities)
└── Audio/ License/ Modulation/ UI/    empty (.gitkeep): no audio, no licensing, no modulation yet
```

All code is in namespace `mu_clid_midi`; only `createPluginFilter` is global.

## Known exception: code compiled from mu-clid/

`Rhythm`, `SequencerEngine`, `RhythmPlayState`, `RhythmParamTable` / `PresetHelpers` (header-only), `EuclideanPanel` and `RhythmCircle` still live in `mu-clid/Source` and are compiled into this target from there (see `CMakeLists.txt`). They move to `mu-core` with the layer container (design-naming §5 step 5.7); until then **do not extend them from this product**, and keep them free of `Plugin/PluginProcessor.h` (the include path holds both products' `Source/`, this one first). The Euclidean generators and the Euclid parameter set (`mu_core::addEuclidParams`) are already in `mu-core`.

## Product rules

- **Layer type stays `Rhythm`, extension `.muRhythm`**, shared with mu-Clid (the one named exception to unique layer-type names).
- **No licensing glue** (never `initLicensing`), **no modulation targets** (so no `mu_mod::ModTarget` table; `check-mod-targets.ps1` lists the product as modulation-free).
- **One rhythm, slot 0.** The processor owns a `SequencerEngine` with exactly one `Rhythm`; there is no sidebar, voice section, mixer or preset browser.
- **Audio-thread rules** as everywhere: `processBlock` allocates nothing, try-locks `rhythmsLock` (a skipped or bypassed block still flushes a pending note-off), and reads the two MIDI parameters through cached pointers. `parameterChanged` can run on the audio thread (VST3 automation): it writes the rhythm fields but defers the pattern rebuild to the message thread (a 30 Hz timer in `PluginProcessor`).
- **New audio-path code goes in `MidiRhythmCore`**, not `PluginProcessor`, so the render tests cover it.
- The host-session and state-restore code runs on the message thread under `suspendProcessing` + `rhythmsLock`.
