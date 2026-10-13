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

## Shared sequencer code (no longer an exception)

`Rhythm`, `RhythmSequencer`, `RhythmPlayState`, `RhythmParamTable` / `RhythmPresetHelpers`, `EuclideanPanel` and `RhythmCircle` live in `mu-core` (Stage 2b, done). This product compiles nothing from `mu-clid/` and `mu-clid/Source` is not on its include path, so its own `Plugin/PluginProcessor.h` is the only one in scope. Do not add a `mu-clid/` include or compile-in; if mu-Clid has code this product needs, it moves to `mu-core` first.

## Product rules

- **Layer type stays `Rhythm`, extension `.muRhythm`**, shared with mu-Clid (the one named exception to unique layer-type names).
- **No licensing glue** (never `initLicensing`), **no modulation targets** (so no `mu_mod::ModTarget` table; `check-mod-targets.ps1` lists the product as modulation-free).
- **One rhythm, slot 0.** The processor owns a `RhythmSequencer` with exactly one `Rhythm`; there is no sidebar, voice section, mixer or preset browser.
- **Audio-thread rules** as everywhere: `processBlock` allocates nothing, takes no lock (only a block that starts during a session restore is skipped; a skipped or bypassed block still flushes a pending note-off), and reads the two MIDI parameters through cached pointers. `parameterChanged` can run on the audio thread (VST3 automation): it only raises the parameter's dirty flag (found by binary search over a table built in the constructor); the message thread (a 30 Hz timer in `PluginProcessor`) reads the parameter's own raw value, writes the rhythm fields and rebuilds the pattern. `rhythmsLock` only serialises those message-thread writers; `voiceParamsLock` is not used here.
- **New audio-path code goes in `MidiRhythmCore`**, not `PluginProcessor`, so the render tests cover it.
- The host-session and state-restore code runs on the message thread under `suspendProcessing` + `rhythmsLock`.
