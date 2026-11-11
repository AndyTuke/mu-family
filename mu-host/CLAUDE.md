# mu-Host — CLAUDE.md

Product-specific guidance for working in `mu-host/`. Read alongside the family-wide [/CLAUDE.md](../CLAUDE.md) and [mu-tronic/CLAUDE.md](../mu-tronic/CLAUDE.md).

## Project overview

**μ-Host** is one mu instrument channel as a plug-in (VST3 + CLAP, no Standalone) by Transwarp Development Project: a drop-down in the transport bar picks any of the family's engines (Rhythm, Pattern, Arp, Kick, Bass, Hat, Snare, Rumble, FM Perc, Chord, Phrase), and the engine's voice, inserts, sequencer, modulation and layer presets are shown, with its output going straight to the DAW for mixing. No sidebar, scenes, pages, clips, mixer or full-preset controls: the DAW mixes and saves. Not yet released: no installer, site pages, manual, CI zip or store product (backlog #1775).

## Build targets

- `mu-host`: the plug-in (VST3 + CLAP; AU on macOS only, which is on hold)
- `mu-host-tests`: console UnitTest runner, **`EXCLUDE_FROM_ALL`**; `cmake --build build --target mu-host-tests`
- `mu-host-uisoak`: the real editor driven in-process with random clicks, keys and engine changes (**`EXCLUDE_FROM_ALL`**); `MU_UISOAK_SHOT=out.png mu-host-uisoak.exe 3 1` also writes a PNG of the window

Artefacts are `mu-Host.vst3` / `.clap` in `build/mu-host/mu-host_artefacts/<Config>/`.

## It builds on the mu-Tronic shell

mu-Host is a product of its own on mu-Tronic's shell. Its target compiles mu-Tronic's sources (the shell, with `MU_MAX_LAYERS=1` and no buses; everything but mu-Tronic's `PluginFactory.cpp`) plus its own `Source/`. Nothing is copied. The seams are small and live in mu-Tronic's files:
- `mu_tronic::PluginProcessor` takes a `ProcessorIdentity` (state tag, settings folder, whether it asks for mu-Tronic's own licence); mu-Host passes its own names and no licence.
- `mu_tronic::PluginEditor` takes `EditorOptions { singleSlot }` (no sidebar, scenes, clips, mixer toggle or full-preset controls) and calls a `slotShown` hook whenever a slot is shown.
- This product's `PluginProcessor` / `PluginEditor` derive from those and add what makes it mu-Host: its names, the engine drop-down, the window size (the standard one less the sidebar's width, so the voice panels keep their standard size), and the licensing below.
- A change to the shell that should reach both is made once in mu-Tronic's files. A change that is only mu-Host's is made here.
- Include rule: mu-Tronic's `Source/` is first on the include path, so `Plugin/PluginProcessor.h` from shell code is the shell's. This product's headers of the same names are reached from its own files (same folder) and by relative path; this product's headers include the shell's with angle brackets, `<Plugin/PluginProcessor.h>`.

## Licensing (Andy, 2026-11-07)

mu-Host has no licence of its own. Opening an engine runs the licence check of the **product** the engine belongs to ([Source/License/EngineLicensing.cpp](Source/License/EngineLicensing.cpp): mu-Clid Rhythm, mu-Tant Pattern, mu-Toni Arp, mu-On Kick / Bass / Hat / Snare / Rumble / FM Perc / Chord, mu-Tilate Phrase), and from then on it is that product's own app: its key (looked up in that product's own content folder and in mu-Host's), activation, checkout page, price and licence reminder (every load, until Maybe later on that instance). A key covers the whole product. `PluginProcessor::licenseEngine` does it; the editor calls it from `slotShown`.

**Free, in every paid app's install (Andy, 2026-11-11, #1803).** mu-Host itself costs nothing and ships in each paid app's zip. Its engine drop-down lists only the engines whose product has a key (`isEngineLicensed` in `EngineLicensing`, which reads the key files without opening an engine), plus the engine already in the slot so a saved project still shows what it holds. With no licensed product and an empty slot, an instance explains itself in a pop-up on load: what mu-Host is (a lightweight single-channel host) and why the list is empty.

## Source layout

```
mu-host/Source/
├── Plugin/        PluginProcessor, PluginEditor (on the mu-Tronic shell)
├── Persistence/   HostState.h (MuHostState, MuHostPreset, .muHost, muHost folder)
├── License/       EngineLicensing (engine -> product licence identity)
├── Tests/         HostTests.cpp (shares mu-tronic's TestMain and processor sources)
└── Audio/ Modulation/ Sequencer/ UI/    empty (.gitkeep): all of it comes from the shell and the layer libraries
```

All code is in namespace `mu_host`; only `createPluginFilter` is global.

## Product rules

- Plugin IDs are saved data once released: `PLUGIN_CODE MHst`, manufacturer `TDP1`, `BUNDLE_ID` / `CLAP_ID` `com.transwarp.mu-host`, state id `MuHostState`.
- No icon of its own yet (it uses mu-Tronic's in the build); no manual, site pages or store product yet.
- Layer presets load and save in the current engine's own product folder (the layer-type registry already says where).
