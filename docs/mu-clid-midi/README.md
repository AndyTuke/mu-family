# mu-Clid MIDI

The former mu-Clid Lite, now a separate, free product: a single-rhythm Euclidean **MIDI effect** (VST3 + CLAP, no audio, no Standalone, no licensing or support prompt).

- Display name: **mu-Clid MIDI**. Plugin IDs are unchanged from mu-Clid Lite (`MCLt`, `TDP1`, `com.transwarp.mu-clid-lite`, state id `MuClidState`) so existing DAW projects keep working.
- Shares mu-Clid's `Rhythm` / `.muRhythm`.
- Source: [mu-clid-midi/](../../mu-clid-midi/CLAUDE.md), target `mu-clid-midi`, namespace `mu_clid_midi` (moved out of `mu-clid/` at stage 3 of the plan in [../design-plugin-family.md](../design-plugin-family.md), "The MIDI-effect product"). Rhythm, RhythmSequencer and the Euclid panels are still compiled from `mu-clid/` until they move to mu-core.
- Parameters: one rhythm's Euclid set (`r0_*`), `mstrLoop`, `lite_midiNote`, `lite_accentAmt`. The voice, mixer and FX parameters the Lite build carried but never used are gone; sessions saved by Lite still restore their rhythm, loop, MIDI note and accent.
- Release notes: renamed from "mu-Clid Lite"; the installer removes the old "mu-Clid Lite" bundles. Hosts that keep favourites or FX chains by name may need the plugin re-picked.
