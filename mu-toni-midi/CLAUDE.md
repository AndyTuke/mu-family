# mu-Toni MIDI — CLAUDE.md

Product-specific guidance for working in `mu-toni-midi/`. Read alongside the family-wide [/CLAUDE.md](../CLAUDE.md) and [mu-toni/CLAUDE.md](../mu-toni/CLAUDE.md).

## Project overview

**μ-Toni MIDI** is mu-Toni's generative arpeggiator as a **MIDI effect** (VST3 + CLAP, no audio, no Standalone), in the same relation to mu-Toni as mu-Clid MIDI has to mu-Clid. The keys held on its MIDI input play the arp's scale / chord notes as MIDI on the host transport; note messages in are consumed, every other message passes through. Not yet released: no licensing, installer, site pages or release wiring exist (backlog #1780).

## Build targets

- `mu-toni-midi`: the plugin (VST3 + CLAP; AU on macOS only, which is on hold)
- `mu-toni-midi-tests`: console UnitTest runner, **`EXCLUDE_FROM_ALL`**; `cmake --build build --target mu-toni-midi-tests`

## Shared code, not copied

- The arp clock and note scan are `mu_toni::BasicArpRunner<Voice>` in [mu-toni/Source/Sequencer/ArpRunner.h](../mu-toni/Source/Sequencer/ArpRunner.h). mu-Toni instantiates it with its synth voice (`ArpVoiceRunner` = `BasicArpRunner<ToniVoice>`); this product instantiates it with `MidiNoteVoice`, which has the same note interface but records MIDI note events at the sample the runner fired them. A change to the arp's timing, note pool, scan, accent or legato is made in `ArpRunner.h` / `ArpSequencer.h` and reaches both.
- The parameter rows are mu-Toni's `addArpRows` (`a0_scale` ... `a0_stepMask`, the same ids and ranges), plus `midiInCh` (mu-Toni's id), `midiOutCh` and `midiVel`. Rows are only ever appended.
- This product compiles `mu-toni/Source/Persistence/ArpParams.cpp` and a few mu-core sources (the same list `mu-toni-layer-tests` links), and puts `mu-toni/Source` on its include path after its own `Source/`, so its `Plugin/PluginProcessor.h` wins over mu-Toni's of the same path.

## Source layout

```
mu-toni-midi/Source/
├── Plugin/      PluginProcessor (host glue: transport, state), PluginEditor, ParameterLayout.h
├── Sequencer/   MidiArpCore (the audio path; no AudioProcessor), MidiNoteVoice (the MIDI stand-in voice)
├── Tests/       juce::UnitTest suite
└── Audio/ License/ Modulation/ Persistence/ UI/    empty (.gitkeep)
```

All code is in namespace `mu_toni_midi`; only `createPluginFilter` is global.

## Product rules

- **No licensing glue**, **no modulation targets** (no `mu_mod::ModTarget` table; `check-mod-targets.ps1` lists the product as modulation-free), no layers (`getNumLayers()` is 0).
- **Audio-thread rules** as everywhere: `processBlock` allocates nothing. New audio-path code goes in `MidiArpCore`, not `PluginProcessor`, so the render tests cover it.
- A note-off goes out on the channel its note-on used, whatever the output-channel setting does in between; a bypassed block or a transport stop never leaves a note sounding.
- Plugin IDs are saved data once released: `PLUGIN_CODE MTnM`, manufacturer `TDP1`, `BUNDLE_ID` / `CLAP_ID` `com.transwarp.mu-toni-midi`, state id `MuToniMidiState`.
