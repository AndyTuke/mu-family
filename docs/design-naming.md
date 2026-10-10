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
| **Performance** (proposed) | A list of up to 8 pointers to presets for a gig; the new top level (backlog #1279). ✔ owner | a preset |
| **Clip** ✔ owner | One of 8 stored states of a layer that a controller pad launches, as in Ableton Live and Bitwig; plays until another is chosen; pads never write presets (backlog #1277). "Slot" stays for FX / insert slots only. | an audio clip, an FX slot |

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

New layer UI is `LayerPanel` / `LayerSidebar` in `mu-core`; a product supplies content, not a copy of the
class.

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
- `Plugin/` holds only the processor (and its `PluginProcessor_<Aspect>.cpp` parts), the editor and the
  standalone entry. Engine, sampler and preset code belongs in `Audio/` and `Persistence/`.
- The processor splits as `PluginProcessor_<Aspect>.cpp` with a fixed aspect set:
  `APVTS`, `Preset`, `Modulation`, `Content`.

### Identifiers
- Members: **no suffix** (`proc`, `sampleRate`). No `m_`, no trailing `_`.
- Constants: `kName`. Enums: `enum class`. Callbacks: `onThing`.
- Capacity constants come from `mu_limits` (`kMaxLayers`); a product does not redeclare them.
- Acronyms: caps in **type** names (`FXChain`, `LFOEditor`, `VUMeter`); word-case in functions and
  variables (`startFxParamSync`, `bindStripToApvts`, `getBpm`, `Midi…`).

### Saved-data names (change only with a migration)
Parameter-id prefixes (`r0_`, `v0_`, `k_ b_ h_ s_ r_`), preset extensions (`.muRhythm`, `.muPattern`,
`.muArp`, `.muTrack`), content folders, state identifiers (`Mu<Product>State`) and plugin codes
(`MCld MTnt MTni MuOn`) are *data*: renaming them touches presets and DAW sessions. **Leave them until
the combined instance is scheduled** — the layer-type registry should own prefixes then. The composed
slot state already stores ids without their prefix, so layer data is prefix-agnostic on disk.

---

## 4. Audit — where the code differs from §3 (2026-10-09)

| Area | Finding |
|---|---|
| Layer noun | Rhythm (mu-Clid, ~1580 uses), Voice (mu-Tant, ~820), Layer (mu-Toni), Lane / Channel / Track (mu-On). |
| Parent class | Only `Rhythm` derives from `Layer`; the others use parallel arrays (§2). |
| mu-core vocabulary | `kMaxChannels` is the layer cap; core types say both `Voice*` and `Channel*`; "Slot" has three meanings (11 types). |
| Namespaces | Done at build 1190: mu-Clid is entirely in `mu_clid` (the Rhythm modulator adapters, preset and param tables and `PluginProcessor_Internal` helpers left `mu_pp`; `mu_pp_migrate` is `mu_clid::migrate`; `md` and `ModDest` nest in `mu_clid`). Only `createPluginFilter` and `StandaloneApp.cpp` stay global. |
| Overlay vs Panel | Done at build 1172: `AboutOverlay`, `ActivationOverlay`, `MidiPresetsOverlay`, `MidiFullPresetsOverlay` (were `…Panel`). Still open: whether `SaveDialog` and `PresetBrowser` become overlays too or stay named exceptions (owner). |
| Main sound panel | `VoiceSection` (mu-Clid), `VoicePanel` (mu-Tant), `EnginePanel` (mu-Toni, mu-On), `VoiceBand` (core). |
| Sequencer names | `SequencerEngine`, `GatePattern`, `Arpeggiator` + `ArpVoiceRunner`, `GrooveSequencer` — no shared role. |
| Capacity constants | mu-Clid's `kMaxRhythms` survives only as a one-line alias of `mu_limits::kMaxLayers` in `HotSwapStager.h`; drop it for `mu_limits::kMaxLayers` directly (open). mu-Tant's two `kMaxVoices` and mu-Toni / mu-On's `kMaxChannels` now read `mu_limits::kMaxLayers` (build 1171). |
| Acronyms | `…APVTS` function names in mu-Clid vs `…Apvts` elsewhere. |
| Members | Trailing `_` in seven classes (mu-Clid helpers, `MidiClockSync`, `VocoderInsert`). |
| Enums | plain `enum` in mu-Toni (3), mu-On (3), mu-Tant (2). |
| Folders | Done at build 1187: mu-Clid now has all eight (`SamplePreview` + `SampleLibrary` in `Audio/`, `RhythmManager` in `Sequencer/`, `PresetIO` + `PresetIO_HostState` in `Persistence/`, `ModulationSkew` in `Modulation/`, `LiteEditor` in `UI/`; `HotSwapStager` / `HotSwapBoundary` stay in `Plugin/` like mu-Tant's stager). Still open: mu-On has no `License` or `Persistence`; mu-Tant and mu-Toni `Persistence/` hold only `.gitkeep`. |
| Saved data | Four param-id schemes; layer-preset extension and folder nouns disagree (mu-Tant: `.muPattern` in `Voices/`); mu-Tant's `.muPattern` already matches its `Pattern` layer type. |

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
   5. **S** central `addLayer` / `removeLayer` / `swapLayers` / `resetLayer`; generic hot-swap stager.
   6. **O** variable layer count in mu-Toni (backlog #1240) and mu-On (#1241), the clip bank (#1277).
   7. **S** mu-Clid's `Rhythm` takes its engine, MIDI engine, play state and samples as members and moves into the container.
   8. **O**, with the combined instance and #1263: the layer-type registry and `Layer::render`.
6. **Saved-data names** — only with the combined instance, with migrations. Backlog #1263 (On Hold).

Separately, the **standard `Source/` layout** (eight folders in every instrument product): folders created
and mu-Clid's misplaced files moved (build 1187); mu-On's missing folders remain. Backlog #1268.

Each step: Debug build + unit tests + the round-trip listening tests on the build PC before the next.

## 6. Open questions


- Is the shared `Layer` polymorphic, or a table of registered layer types? **Settled 2026-10-10:**
  polymorphic (virtual hooks), with a registry that only creates and describes layer types (§2).
- Do the ~130 global `mu-core` types move into `mu_*` namespaces, or stay as the shared "house" types?
