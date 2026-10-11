# mu-Clid MIDI

The former mu-Clid Lite, now a separate, free product: a single-rhythm Euclidean **MIDI effect** (VST3 + CLAP, no audio, no Standalone, no licensing or support prompt).

- Display name: **mu-Clid MIDI**. Plugin IDs are unchanged from mu-Clid Lite (`MCLt`, `TDP1`, `com.transwarp.mu-clid-lite`, state id `MuClidState`) so existing DAW projects keep working.
- Shares mu-Clid's `Rhythm` / `.muRhythm`.
- Today it is still built from `mu-clid/` (target `mu-clid-lite`, macro `MUCLID_LITE_BUILD`). The staged move to its own `mu-clid-midi/` folder is in [../design-plugin-family.md](../design-plugin-family.md) ("The MIDI-effect product").
- Release notes: renamed from "mu-Clid Lite"; the installer removes the old "mu-Clid Lite" bundles. Hosts that keep favourites or FX chains by name may need the plugin re-picked.
