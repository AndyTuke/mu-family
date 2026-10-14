# μ Family — Naming Conventions

How the apps name things, so the family reads as one codebase. Written 2026-10-09 from a survey of every
source tree (script + reading; nothing compiled). Items marked **✔ owner** are decisions; the rest are
**proposed** until confirmed.

This serves the one-framework / one-instance direction in [design-future.md](design-future.md): every
shared concept gets one name in `mu-core`, and a product adds only what makes it different (its sound
engine and its sequencer). Rule of thumb for any rename: *does it make combining the engines in one
place easier or harder?*

Related: the family consistency rule in [CLAUDE.md](../CLAUDE.md), and the platform contract in
[design-plugin-family.md](design-plugin-family.md).

---

## 1. The vocabulary

| Term | Means | Not used for |
|---|---|---|
| **Layer** ✔ owner | One engine + one sequencer + one mixer channel. The parent class every product's layer type derives from. | — |
| **Engine** | The sound-producing part of a layer (synth voice bank, kick, sampler, arp voice). | the whole layer |
| **Sequencer** | The part of a layer that decides *when* (Euclidean hits, gate pattern, arp scan, step grid). | the whole layer |
| **Channel** | The mixer strip only (`MixerChannel`, `ChannelState`). One layer feeds one channel. | a layer |
| **Slot** | An FX or insert slot only (`DelaySlot`, `EffectSlot`, `FXSlotBase`, insert slots). | a layer |
| **Voice** | One polyphonic voice *inside* an engine (`ToniVoice`, `SynthVoice`). | a layer |
| **Preset** | A saved full state of one product: up to 8 layers with their mixer, FX and modulation (`.muClid`, `.muTant`, ...). | a layer preset |
| **Layer preset** | One layer's saved state (`.muRhythm`, `.muPattern`, ...). | a full preset |
| **Performance** (built 2026-10-13) | A list of up to 8 pointers to presets for a gig; the new top level (backlog #1279). ✔ owner. File `.muPerformance`, folder `Performances/`, type `mu_pp::PerformanceFile` (ruled 2026-10-12, [design-launchpad.md §3.2c](design-launchpad.md)). | a preset |
| **Clip** ✔ owner | One of 8 stored states of a layer that a controller pad launches, as in Ableton Live and Bitwig; plays until another is chosen; pads never write presets (backlog #1277). Stored in `mu_core::ClipBank` (`Layer::clips`), saved as `<Clips>` / `<Clip>`; a clip has no per-product type, it is the product's layer preset. "Slot" stays for FX / insert slots only. | an audio clip, an FX slot |

### Derived layer types ✔ owner (2026-10-09)

Each product's layer is a subclass of `Layer`, named for what that product's layer *is*:

| Product | Layer type | Adds to `Layer` |
|---|---|---|
| mu-Clid | `Rhythm` | Euclidean generators, sample binding |
| mu-Tant | `Pattern` | gate / filter / pitch patterns, wavetable |
| mu-Toni | `Arp` | scale / chord pool, scan, accent pattern |
| mu-On | `Track` | step rows, lane engine selection |

Notes on these names:

- **mu-Tant is `Pattern`** (owner, 2026-10-09). It matches the layer files it already saves
  (`.muPattern`), and it keeps every product's layer type name unique (`Rhythm`, `Pattern`, `Arp`, `Track`),
  so the layer-type registry and the preset extensions never collide. (An earlier note considered `Rhythm`
  for mu-Tant; it was dropped because mu-Clid already owns that name and `.muRhythm`.) Its code currently
  calls a layer a *voice*; that word is kept only for a polyphonic voice inside an engine (`SynthVoice`).
- **`Pattern` next to `GatePattern` and `StepPattern`.** mu-Tant's `GatePattern` (and its filter / pitch
  patterns) become *members* of its `Pattern` layer, and mu-On's `StepPattern` belongs to a different
  product and namespace, so there is no clash; it just reads as "a Pattern holds Patterns". If that proves
  confusing, rename the members (`GateLane`, say) rather than the layer.
- **The MIDI-effect product (the former mu-Clid Lite, a separate product from 2026-10-11)** uses the
  same `Rhythm` layer type and `.muRhythm` extension as mu-Clid, because both use one shared class in
  `mu-core`. This is the single named exception to "layer type names are unique"; see
  [design-plugin-family.md](design-plugin-family.md#the-midi-effect-product-the-former-mu-clid-lite--ruled-2026-10-11).
- **mu-On is `Track`** (owner); its extension is already `.muTrack`. The code mixes lane / channel /
  track today and collapses to Track.

---

## 2. The Layer parent class

**Today.** `Layer` ([mu-core/Sequencer/Layer.h](../mu-core/Sequencer/Layer.h)) is the intended
parent: it holds `voiceParams`, the `controlSequences`, the `modulationMatrix`, the two spin-locks,
`name` and `colourIndex`. But only mu-Clid's `Rhythm` actually derives from it (`class Rhythm : public
Layer`). The other three products keep a plain `Layer` in an array and store the rest of each
layer in separate parallel arrays on the processor:

| Product | Per-layer data, held in arrays indexed by layer |
|---|---|
| mu-Clid | `std::vector<Rhythm>` (derived — the closest to the target) |
| mu-Tant | `voiceSlots`, `gatePatterns`, `filterPatterns`, `pitchPatterns`, `inserts`, `voiceRingBuffers` |
| mu-Toni | `voiceSlots`, `runners`, `inserts`, `insCfg` |
| mu-On | `voiceSlots` (engines and step data live elsewhere) |

**Target.** A product's derived type (`Rhythm`, `Pattern`, `Arp`, `Track`) holds everything that
layer has, including what the parallel arrays hold today, and `mu-core` adds / reorders / deletes
layers through the `Layer` base without product code.

**Ruling (Architecture Steward, 2026-10-10; owner decisions marked).** The full contract, the staged
plan and the test per stage are in [design-plugin-family.md, Layer ownership and structural
edits](design-plugin-family.md#layer-ownership-and-structural-edits--family-standard). In short:

- **Dispatch: virtual functions on `Layer`, plus a registry that only constructs.** Per-layer
  behaviour is virtual, called once per layer per block or per edit, never per sample
  (`typeId`, `writeExtras` / `applyExtras`, `resetToDefaults`, `onMoved`, `insertGainReduction`,
  later `render`). A registered layer-type table (`id`, name, param prefix, preset extension, mod-target
  table, `create`) is added only when a second layer type can share one instance; it creates and
  describes layers, it does not dispatch.
- **Ownership: `ProcessorBase` owns the layers**, in a fixed-capacity `std::array<std::unique_ptr<Layer>,
  mu_limits::kMaxLayers>` with an atomic count and one lock. Never a `std::vector`.
- **`Layer` stays data-light.** It keeps what it has (`voiceParams`, control sequences, matrix, the two
  locks, `name`, `colourIndex`) and gains virtuals and the message-thread-only clip bank. Shared
  sub-objects (the insert stage) are composed members from `mu-core`, not base-class data, so `Rhythm`
  stays cheap and the engine, insert and gates travel with the layer when it moves.
- **What stays product-side:** the sequencer data, the engine, the product's `ModTarget` table, its
  param table and prefixes, its editor panels, and (until a second type shares an instance) the typed
  accessor (`getRhythm(i)`, `getPattern(i)`).
- **`ProcessorBase` layer accessors (done):** `getNumLayers`, `getLayer(i)`, `getMaxLayers`, `getNumActiveLayers`, `getLayerName(i)`, `getLayerColourIndex(i)`. They replaced `getNumChannels`, `getMaxChannels`, `getNumActiveChannels`, `getChannelName` and `getChannelColourIndex`; mu-Tant's duplicate `getNumVoices` was removed in favour of `getNumLayers`. `juce::AudioBuffer::getNumChannels` is an audio-channel count and keeps its name. Saved identifiers (`numVoices`, `voiceColours`) are unchanged.

---

## 3. Rules

### Namespaces
- Every product type lives in `mu_<product>` (`mu_tant`, `mu_toni`, `mu_on`, `mu_clid`). All four comply
  (mu-Clid since build 1190); only `createPluginFilter()` and `StandaloneApp.cpp` glue stay global.
- Product helpers that were once namespaced by area nest in the product namespace (`mu_clid::migrate`,
  `mu_clid::md`, `mu_clid::ModDest`).
- Product code never goes inside a `mu-core` namespace.
- **Overload rule:** when a product adapter overloads a core function (e.g. `serialiseModulators(const
  Rhythm&)` beside core's `Layer` version) and is moved from the core namespace into the product namespace,
  every call into the core version must now be qualified (`mu_pp::serialiseModulators(...)`) and any stale
  `using mu_pp::name;` line must be re-pointed or removed. Otherwise a `Rhythm` silently binds to the `Layer`
  overload (derived-to-base) and compiles without error. After such a move, grep the product for unqualified
  calls to the name.
- `mu-core` uses `mu_core`, `mu_ui`, `mu_audio`, `mu_mod`, `mu_pp`, `mu_link`, … by area. The ~130
  `mu-core` types that are still global (filters, UI components) are left alone for now.
- `createPluginFilter()` stays global (JUCE requires it).
- `using namespace juce;` only at function scope in a `.cpp`, never in a header.

### Class-name roles

| Role suffix | Meaning |
|---|---|
| `Panel` | an inline region of the editor |
| `Overlay` | a full-editor screen shown through `OverlayHost` (`SettingsOverlay`, `MixerOverlay`) |
| `Sidebar` / `Section` / `Subsection` / `Row` / `Bar` | as their names say |
| `Engine` / `Sequencer` | the two halves of a layer |
| `Backend` | the interface an app implements for the shared chrome (`mu_core::ShellBackend`; `ProcessorBase` and mu-link's `LinkShellBackend` implement it). Not `Surface`: that word is the controller surface of `mu-control` |
| `Driver` / `Manager` | `mu-control` only: a `<Device>Driver` speaks one device family's wire protocol and nothing else (`LaunchpadXDriver`, behind the `SurfaceDriver` interface); a `Manager` owns the lifetime of a set of drivers and their model (`ControllerManager`). The four parts are `SurfaceDriver`, `SurfaceDescription`, `SurfaceMapping`, `SurfaceModel` |
| `Chrome` | a processor-free helper that owns the shared window furniture for a host component (`mu_ui::ShellChrome`) |

New layer UI is `LayerPanel` / `LayerSidebar` in `mu-core`; a product supplies content (`<Type>Content`) and
a mini-graphic (`<Type>MiniVisual`), not a copy of the class.

**Named exceptions ✔ owner (2026-10-10):** `SaveDialog` and `PresetBrowser` (both `mu-core/UI`) keep their
names. They are modal pop-ups, not `OverlayHost` screens, so the `Overlay` role does not apply.

### Files and folders
- File name = class name. One main class per file.
- **Identical `Source/` layout in every instrument product ✔ owner (2026-10-09):** mu-Clid, mu-Tant, mu-Toni
  and mu-On each have exactly these eight subfolders, whether or not they have anything to put in them:
  **`Audio`, `License`, `Modulation`, `Persistence`, `Plugin`, `Sequencer`, `Tests`, `UI`**. An empty
  folder carries a `.gitkeep` so git keeps it. A new product starts with all eight. (mu-link is
  infrastructure, not an instrument, and keeps its own layout.)
- What goes where: `Audio/` the engine (voices, samplers, DSP specific to the product); `Sequencer/` the
  sequencer data and generators; `Modulation/` the product's modulation-target table and snapshot;
  `Persistence/` preset and state (de)serialisation and migrations; `License/` licensing glue; `UI/` the
  panels, overlays and sidebars; `Tests/` the unit tests.
- `mu-core/` folders are by role: `Audio, Control, License, Link, Modulation, Persistence, Plugin,
  Sequencer, UI`. `Control/` (checked 2026-10-10) holds the device-independent controller-input engine;
  its names follow the standard (file = class, `mu_core` namespace, neutral terms, no product names).
  Remark: the struct `mu_core::Mapping` in `MidiControlMap.h` is too generic for a namespace shared by
  the whole core; rename it `MidiMapping` the next time that file is touched.
- `mu-control/` (ruled 2026-10-13; namespace `mu_control`, device helpers nest as `mu_control::launchpadx`) has folders by role, none of which may repeat a
  `mu-core` path: `Surface, Driver, Devices/<Device>, Manager, Tests`. Full list and file names in
  [design-plugin-family.md, Controller library](design-plugin-family.md#controller-library-mu-control--family-standard-ruled-2026-10-13-backlog-1276).
  The target is `mu-control` (INTERFACE) and its tests `mu-control-tests`; the build option is `MUFAMILY_WITH_CONTROL`.
- `Plugin/` holds only the processor (and its `PluginProcessor_<Aspect>.cpp` parts), the editor and the
  standalone entry. Engine, sampler and preset code belongs in `Audio/` and `Persistence/`. The
  top-level `PluginEditor.{h,cpp}` (the `EditorShellBase` subclass) is in `Plugin/` in every product
  (mu-Clid, mu-Tant, mu-Toni, mu-On, mu-Clid MIDI; ruled 2026-10-11), next to the processor it pairs
  with; `UI/` holds only what the editor composes (panels, overlays, sidebars, `LiteEditor`-style
  secondary editors). A product with no panels of its own keeps `UI/` empty with a `.gitkeep`.
- The processor splits as `PluginProcessor_<Aspect>.cpp` with a fixed aspect set:
  `APVTS`, `Preset`, `Modulation`, `Content`.

### Identifiers
- Members: **no suffix** (`proc`, `sampleRate`). No `m_`, no trailing `_`.
- Constants: `kName`. Enums: `enum class`. Callbacks: `onThing`.
- Capacity constants come from `mu_limits` (`kMaxLayers`); a product does not redeclare them.
- Acronyms: caps in **type** names (`FXChain`, `LFOEditor`, `VUMeter`); word-case in functions and
  variables (`startFxParamSync`, `bindStripToApvts`, `getBpm`, `Midi…`).

### Saved-data names (change only with a migration)
Parameter-id prefixes (`r0_`, `v0_`, `a0_`, `t0_`), preset extensions (`.muRhythm`, `.muPattern`,
`.muArp`, `.muTrack`), content folders, state identifiers (`Mu<Product>State`) and plugin codes
(`MCld MTnt MTni MuOn`) are *data*: renaming them touches presets and DAW sessions. The composed
slot state already stores ids without their prefix, so layer data is prefix-agnostic on disk. The
composed state's root properties `numVoices` (live layer count) and `voiceColours` (8 palette indices) are
saved data too: they keep their names in every product (mu-Tant today; mu-Toni and mu-On with #1240 / #1241,
read and written by `ProcessorBase`), even though "voice" is the retired layer word. A state without
`numVoices` means the product's legacy count (mu-Tant 1, mu-Toni 4, mu-On 5).

**Ruled 2026-10-11 (architecture-steward, backlog #1263; the owner scheduled this ahead of variable
layers).** What changes, and what deliberately does not. **Status 2026-10-12: the prefix renames are done
(steps 0-2 below); only the optional `Voices/` to `Patterns/` folder move remains, and it is on hold.**

| Name | Today | Becomes | Why |
|---|---|---|---|
| Layer parameter-id prefix, mu-On | `k_ b_ h_ s_ r_` (by lane type; `r_` is Rumble, mu-Clid's `r0_` is a rhythm) | `t0_` ... `t4_` (`t` = Track, index = layer slot) | Variable layers and a per-layer engine selector (#1241) need index-based prefixes; this is the only change they require. Lane 0 Kick, 1 Bass, 2 Hat, 3 Snare, 4 Rumble keep their slot numbers, so `k_tune` becomes `t0_tune`, `r_x` becomes `t4_x`. Mod-target `id`s (saved in modulator assignments) do not change, only their `parameter` field. |
| Layer parameter-id prefix, mu-Toni | `v0_` ... | `a0_` ... (`a` = Arp) | Pre-release (0.1). Makes the four prefixes unique (`r` Rhythm, `v` Pattern, `a` Arp, `t` Track), which the layer-type registry needs the day two types share an instance, and frees `v` for its vocabulary meaning (a voice inside an engine). |
| mu-Clid `r0_`, mu-Tant `v0_` | — | **unchanged** | Live products (1.1). The ids are DAW automation targets and MIDI-map keys, nothing in the code needs them to change, and the registry holds a prefix per type (`Pattern` keeps `v`, a named legacy letter). Do not rename them without a reason that outweighs breaking users' automation lanes. |
| Mixer / FX ids `ch{N}_`, `mstr_`, ... | — | unchanged | Already one family scheme. |
| Layer-preset extensions | `.muRhythm .muPattern .muArp .muTrack` | unchanged | Each already is `.mu<LayerType>`. |
| Layer-preset folder, mu-Tant | `Voices/` | `Patterns/` | The only folder that disagrees (`Rhythms`, `Patterns`, `Arps`, `Tracks`). **Dropped for now (2026-10-12): optional, cosmetic, on hold pending Andy's confirmation; nothing depends on it.** |
| Plugin codes `MCld MTnt MTni MuOn`, `MCLt` | — | **unchanged** | `MuOn` breaks the pattern but changing it only re-identifies the plugin to every host; no code needs it. |
| State ids `Mu<Product>State` | — | unchanged | Already consistent. |

**Migration contract.**
- *Format-2 states (full presets, layer presets, host sessions) need nothing:* rows carry the id without its
  prefix, so a file written before a rename loads after it. A unit test per renamed product loads a file
  captured before the rename and asserts every parameter.
- *Legacy APVTS dumps* (`<PARAM id="k_tune">`, read by `composeLegacyState`) keep loading: `LayerLayout` gains
  `addLegacyPrefixes(const juce::StringArray&)` (one list per renamed product, same slot order), consulted
  only by `composeLegacyState` when the current prefixes match nothing. Never by live ids.
- *MIDI control maps* (`<product>_midiControl.json`, `ControlAction::paramId`) hold parameter ids:
  `MidiControlMap::setIdPrefixRenames({ {"k_", "t0_"}, ... })`, set by the product before `load()`, rewrites
  a loaded id that starts with an old prefix and saves once. Without it every user mapping on a renamed
  parameter silently goes dead.
- *Host automation* on a renamed id is lost; no code can migrate it. This is why only the two pre-release
  products change, and why it is done once, before they ship publicly.

**User-visible effect of the done renames (2026-10-12).** In mu-Toni and mu-On, automation lanes recorded
against the old ids (`v0_...`, `k_...`) no longer find their parameter and must be re-assigned. Everything else
migrates: full presets, layer presets and host sessions (prefix-free rows), old APVTS dumps (legacy prefix
list) and MIDI control maps (rewritten on load, saved once). mu-Clid and mu-Tant are untouched.
- *Folder move* (`Voices/` to `Patterns/`): in `ensureContentFolders`, if `Voices/` exists and `Patterns/` does
  not, `moveFileTo`; then rewrite the program-change map entries (`midiPresetMap` stores file paths) whose
  path lies under the old folder. If the move fails, leave the folder and keep reading it.

**Order (each step one product, a Debug build, unit tests and `*_roundtrip` between steps):**
0. **Done (148083a).** *No data change, do first:* one `layerParamPrefix(i)` / `layerParamId(i, suffix)` per product replaces every
   literal (`"v" + String(i) + "_"` in mu-Toni's `PluginProcessor.cpp`, `PluginProcessor_Preset.cpp` and
   `EnginePanel.cpp` (now `ArpContent`); `kLanePrefix` and the `"k_..."` literals in mu-On's processor, `GrooveVoices.h`,
   `MuOnModDest.h`, `EnginePanel.cpp` (now `TrackContent`)). After this a prefix rename is one constant. (Test files may keep
   literals; they are the check.)
   Infrastructure that landed with step 1 (34151a1): `LayerLayout::addLegacyPrefixes`,
   `MidiControlMap::setIdPrefixRenames`, and the legacy list passed through `ProcessorBase::initLayerState`.
1. **Done (34151a1 infrastructure, ccbd3b3 rename).** mu-Toni `v` to `a` with its legacy list and the MIDI-map rename.
2. **Done (f703570).** mu-On `k b h s r` to `t{N}` likewise.
3. **Dropped for now, on hold (2026-10-12; Andy to confirm).** mu-Tant `Voices/` to `Patterns/`. Optional and
   cosmetic; if revived, follow the folder-move rule in the migration contract.
#1241 (mu-On variable layers) and the registry (stage 8) can now rely on index prefixes everywhere but
the two named legacy ones.

**Product renames are display-only.** A product's name, slug, folder and namespace may change; its plugin
code, manufacturer code, bundle id, CLAP id and state identifier may not (existing DAW projects find the
plugin by them). The MIDI-effect product keeps `MCLt`, `com.transwarp.mu-clid-lite` and `MuClidState`
under its name, **mu-Clid MIDI** (owner, 2026-10-11).

**Product naming pattern.** `mu-<Name>` in prose and artefacts (`mu-Clid`, `mu-Tant`), slug `mu-<name>` in
folders and targets, namespace `mu_<name>`. A descriptive suffix (`mu-Clid MIDI`) is allowed only for a
product that is a deliberate sibling of another; a standalone product takes a one-word pun like the rest.
The one use today is **mu-Clid MIDI**: slug `mu-clid-midi`, namespace `mu_clid_midi`, docs `docs/mu-clid-midi/`
(hyphens in the slug, underscores in the namespace, as for every product). Its layer type is `Rhythm` and
its preset extension `.muRhythm`, shared with mu-Clid: the named exception to "layer type names are
unique", legitimate because the type is one `mu-core` class (§1).

---

## 4. Audit — where the code differs from §3 (2026-10-09)

| Area | Finding |
|---|---|
| Layer noun | Rhythm (mu-Clid, ~1580 uses), Voice (mu-Tant, ~820), Layer (mu-Toni), Lane / Channel / Track (mu-On). |
| Parent class | Only `Rhythm` derives from `Layer`; the others use parallel arrays (§2). |
| mu-core vocabulary | `kMaxChannels` is the layer cap; core types say both `Voice*` and `Channel*`; "Slot" has three meanings (11 types). |
| Namespaces | Done at build 1190: mu-Clid is entirely in `mu_clid` (the Rhythm modulator adapters, preset and param tables and `PluginProcessor_Internal` helpers left `mu_pp`; `mu_pp_migrate` is `mu_clid::migrate`; `md` and `ModDest` nest in `mu_clid`). Only `createPluginFilter` and `StandaloneApp.cpp` stay global. |
| Overlay vs Panel | Done at build 1172: `AboutOverlay`, `ActivationOverlay`, `MidiPresetsOverlay`, `MidiFullPresetsOverlay` (were `…Panel`). **`SaveDialog` and `PresetBrowser` stay as named exceptions ✔ owner (2026-10-10):** they are modal pop-ups owned by the shell, not `OverlayHost` screens, and their names are already stable across the family; do not rename them. Still open: the shared `LayerPanel` / `LayerSidebar`, now ruled (2026-10-12, family doc "Shared layer sidebar and layer panel"): `ChannelSidebar` is renamed `LayerSidebar`, `SidebarItem` keeps its name, `ChannelHeaderBar` became `LayerHeaderBar` (done 2026-10-12), no product subclasses. |
| Main sound panel | `<Type>Content` (`RhythmContent`, `PatternContent`, `ArpContent`, `TrackContent`). **Ruled 2026-10-12, confirmed 2026-10-14:** the per-layer stack is the shared `LayerPanel`; the product's band inside it is `<Type>Content`. `EnginePanel` is retired and `VoicePanel` no longer exists. `VoiceBand` (core) is the shared voice section inside a content band; `VoiceSection` is kept as the mu-Clid subclass of `VoiceBand`, a section inside `RhythmContent` (not the main panel), named to match the `kVoiceSectionH` token. Mini-graphics are `<Type>MiniVisual`. |
| Sequencer names | `RhythmSequencer` (was `SequencerEngine`; renamed 2026-10-12, file and class together), `GatePattern`, `ArpSequencer` + `ArpVoiceRunner`, `GrooveSequencer`. **Ruled 2026-10-12:** `Arpeggiator` to `ArpSequencer` (done); `GrooveSequencer` conforms; `GatePattern` is data and stays. Main sound panel: `<Type>Content` inside the shared `LayerPanel` (superseding the earlier `EnginePanel` choice), done with #1505 / #1506 (see the family doc, "Shared layer sidebar and layer panel"). |
| Capacity constants | (Done 2026-10-12 with #1240 / #1241: mu-Toni's and mu-On's `kNumChannels` are deleted; mu-On's `EngineType`, `TrackParams.h`, `getTrack` and the engine-qualified suffixes are built as ruled in §5 step 6.) mu-Clid's `kMaxRhythms` alias is gone (build 1195). mu-Tant's two `kMaxVoices` and mu-Toni / mu-On's `kMaxChannels` read `mu_limits::kMaxLayers` (build 1171) but are still redeclared product-side; replace with `mu_limits::kMaxLayers` directly (mu-On's is unused, delete it). The `kMaxChannels` in `mu-core/Link/` is the stereo ring width, an unrelated constant: leave it. |
| `Slot` in layer APIs | `ProcessorBase` and `mu_pp` use "slot" for a layer (`initSlotState`, `slotLayer`, `saveSlotPreset`, `voiceSlots`, ...). Rename to `Layer` per §1; the XML tags `Slots` / `Slot` and `idx` are saved data and stay. Plan: see the rename list under migration step 5. |
| Acronyms | `…APVTS` function names in mu-Clid vs `…Apvts` elsewhere. |
| Members | Trailing `_` in seven classes (mu-Clid helpers, `MidiClockSync`, `VocoderInsert`). |
| Enums | plain `enum` in mu-Toni (3), mu-On (3), mu-Tant (2). |
| Folders | Done at build 1187: mu-Clid now has all eight (`RhythmManager` was deleted by the stage 7 container move; `SamplePreview` + `SampleLibrary` in `Audio/`, `PresetIO` + `PresetIO_HostState` in `Persistence/`, `ModulationSkew` in `Modulation/`, `LiteEditor` in `UI/`; `HotSwapStager` stays in `Plugin/` like mu-Tant's stager (`HotSwapBoundary.h` was deleted 2026-10-13 for the shared predicates)). Still open: mu-On has no `License` or `Persistence`; mu-Tant and mu-Toni `Persistence/` hold only `.gitkeep`. |
| Products | mu-Clid Lite is still a second target in `mu-clid/` (`MUCLID_LITE_BUILD`, about 60 `#if` sites, `LiteEditor` in mu-Clid's `UI/`, namespace `mu_clid`). Owner made it a separate product on 2026-10-11: it is named mu-Clid MIDI (2026-10-11) and gets `mu-clid-midi/` and `mu_clid_midi` by the staged plan in the family doc (stage 1 identity first; stage 2 moved the pure generators to `mu-core/Sequencer/`). The product exists and compiles nothing from `mu-clid/`: `Rhythm`, `RhythmSequencer`, `EuclideanPanel` and `RhythmCircle` moved to `mu-core` (Stage 2b of the family doc, done; the known exception is closed, backlog #1411). |
| Saved data | Parameter-id prefixes done 2026-10-12: one index scheme per layer type (`r0_` Rhythm, `v0_` Pattern, `a0_` Arp, `t0_` Track), each product via `layerParamPrefix` / `layerParamId`. Open (optional, on hold): mu-Tant's folder `Voices/` disagrees with its `.muPattern`. See §3 "Saved-data names". |

---

## 5. Migration order (safest first)

C++ identifier changes never touch saved files; the data renames at the end do.

1. **Cosmetic, any product, any time:** trailing `_` → none; plain `enum` → `enum class`; `…APVTS` →
   `…Apvts`; drop the redeclared `kMax*` in favour of `mu_limits`. (Do the `MidiClockSync` one while
   rewriting it for the sync work.) Backlog #1267.
2. **mu-Clid into `mu_clid`** — done (build 1190). Lesson recorded as the overload rule in §3 Namespaces.
   Backlog #1262.
3. **Overlay / Panel renames** for the shell screens; shared `LayerPanel` / `LayerSidebar` in `mu-core`. Backlog #1266.
4. **`VoiceSlot` → `Layer`** (done, backlog #1265 step 1). The hoist into `Layer` (backlog #1265 steps
   2-3) and the derived types (step 5) are one sequence, listed under step 5: derived types as plain
   value members first, then the shared hooks, then ownership, then central structural edits. Each
   stage is one commit per product, with the unit tests and the round-trip listening tests between stages.
5. **Derived layer types** (mu-Tant `Pattern`, mu-Toni `Arp`, mu-On `Track`; mu-Clid's `Rhythm` already
   exists) replace the parallel arrays; sequencer role names (`<X>Sequencer`) follow. Backlog #1264.
   The stages (M = mechanical, can run unattended; S = supervised, touches the audio thread or a
   lock, run with listening tests on the build PC; O = needs the owner):
   1. **M** derived types as value members, parallel arrays folded in (Tant, Toni, On).
   2. **M** `ProcessorBase::getLayer(i)` accessor and the generic channel name / colour / modulator-panel use of it.
   3. **M** persistence virtuals `writeExtras` / `applyExtras`; `initSlotState` takes only the prefixes.
   4. **S** ownership moves into `ProcessorBase` (array of `unique_ptr<Layer>`, count, `layersLock`).
   5. **S** central `addLayer` / `removeLayer` / `swapLayers` / `resetLayer`; generic hot-swap stager. **Done** (part 1 commit a41c5fd; part 2 commit 8a8be28, `LayerSwapper` in `ProcessorBase`; mu-Clid's own stager remains until stage 7).
   6. **O** variable layer count in mu-Toni (backlog #1240) and mu-On (#1241), the clip bank (#1277). Order, saved
      shape and per-product recipe: design-plugin-family "Stage 6 build rulings". Names ruled 2026-10-12 (apply
      with the work, so each file is touched once): mu-Toni and mu-On delete their `kNumChannels` (use
      `mu_limits::kMaxLayers`); mu-On's `enum Channel { Kick .. Rumble, kNumChannels }` becomes `enum class EngineType : int
      { Kick = 0, Bass, Hat, Snare, Rumble }` plus `kNumEngineTypes` (the values are the saved `t{N}_eng` choice index,
      so they are fixed), `MuOnChannels.h` is renamed `TrackParams.h` (file = what it holds; it stays where mu-Toni keeps its
      equivalent prefix helpers, to mirror), `voiceSlot(lane)` becomes `getTrack(i)`, and "lane" / "channel" in identifiers becomes
      "track" for the layer and "engine" for the sound type; `laneOf` goes (the engine is a parameter, not an identity);
      mu-On's engine-qualified parameter suffixes are `kick_`, `bass_`, `hat_`, `snare_`, `rumble_` (the engine's lower-case
      name, as in the family doc). The processor entry points are the base's `addLayer` / `removeLayer` / `swapLayers` /
      `resetLayer`; no product adds `addTrack` / `addArp` wrappers.
   7. **S** mu-Clid's `Rhythm` takes its engine, MIDI engine, play state and samples as members and moves into the container.
   8. **O**, with the combined instance and #1263: the layer-type registry and `Layer::render`.
   **"Slot" to "Layer" in C++ APIs (Architecture Steward, 2026-10-10).** Do now (pure identifiers, word-bounded
   search/replace, `mu_pp` and `ProcessorBase` first, then each product): `initSlotState` → `initLayerState`
   (first rename mu-Toni's own `initLayerState()` wrapper, which would hide it), `slotLayout` → `layerLayout`,
   `slotExtras` → `layerExtras`, `captureSlotNode` / `applySlotNode` → `captureLayerNode` / `applyLayerNode`,
   `mu_pp::captureSlot` / `applySlot` / `findSlotNode` / `numSlots` / `kSlotTag` / `kSlotsTag` →
   `captureLayer` / `applyLayer` / `findLayerNode` / `numLayers` / `kLayerTag` / `kLayersTag` (the tag
   strings `"Slot"`, `"Slots"`, `"idx"` stay), `saveSlotPreset` / `loadSlotPreset` → `saveLayerPreset` /
   `loadLayerPreset`, `onSlotPresetCommitted` → `onLayerPresetCommitted`, `getPerSlotPresetDir` /
   `Extension` → `getLayerPresetDir` / `Extension`, `applyMidiPresetSlot` → `applyMidiPresetLayer`, the
   product-local `kMaxChannels` / `kMaxVoices` → `mu_limits::kMaxLayers`. `resetSlot` → `resetLayer` is done
   (stage 5 part 1, central in `ProcessorBase`; mu-Clid still has its own until stage 7). Defer: `slotLayer` (disappears with stage 4 ownership), `voiceSlots` and the
   `voiceSlot(i)` accessors (replaced by the derived-type members in stage 1). Keep:
   `captureComposedState` / `applyComposedState` / `toComposedState` ("composed" is not "slot"; the plain
   names `captureState` / `applyState` are taken by the products' own wrappers, and renaming onto them makes
   mu-Toni and mu-On recurse into themselves).
   Related and interlocking: the **MIDI-effect product split** (family doc, "The MIDI-effect product")
   extracts mu-Clid's Euclidean generators (done) and then `Rhythm` / `RhythmSequencer` /
   `RhythmPlayState` / the param table / `EuclideanPanel` / `RhythmCircle` into `mu-core` (Stage 2b in that
   section, ruled 2026-10-11, **before** stage 7: the class is pure today, and stage 7 then uses
   `mu_core::Rhythm` directly instead of moving the shared class again; amended 2026-10-12: no derived `mu_clid::Rhythm`, see
   design-plugin-family "Stage 7 implementation plan").
6. **Saved-data names** — ruled 2026-10-11; prefix renames done 2026-10-12 (the mu-Tant folder move is dropped, on hold): the exact changes, the migration contract and the order are in §3 "Saved-data names". Backlog #1263.

Separately, the **standard `Source/` layout** (eight folders in every instrument product): folders created
and mu-Clid's misplaced files moved (build 1187); mu-On's missing folders remain. Backlog #1268.

Each step: Debug build + unit tests + the round-trip listening tests on the build PC before the next.

## 6. Open questions


- Is the shared `Layer` polymorphic, or a table of registered layer types? **Settled 2026-10-10:**
  polymorphic (virtual hooks), with a registry that only creates and describes layer types (§2).
- Do the ~130 global `mu-core` types move into `mu_*` namespaces, or stay as the shared "house" types?
