# mu-Toni — CLAUDE.md

Product-specific guidance for working in `mu-toni/`. Read alongside the family-wide [/CLAUDE.md](../CLAUDE.md).

## Status

**Platform scaffold — engine TBD.** mu-toni builds as a real plugin (**Standalone + VST3 + CLAP**, in the root build) on top of the complete shared platform: the entire editor shell (TransportBar / StatusBar / About / overlays / window sizing / `MuLookAndFeel`), the shared `ChannelSidebar`, and the shared `MixerOverlay` are all wired and functional. The **product-specific synth engine + sequencer are not yet defined** — `Source/UI/EnginePanel.h` is a labelled blank placeholder where they'll go.

The point of this scaffold: it demonstrates the platform is "done" — a new product gets the full UX/mixer for free; only the engine area is blank.

### What's wired

- **`mu_toni::PluginProcessor : ProcessorBase`** ([Source/Plugin/PluginProcessor.{h,cpp}](Source/Plugin/PluginProcessor.cpp)) — APVTS via `mu_mixfx::addGlobalFxParams` + per-layer `ch{N}_` strips; routes a **silent** render through the shared `MixerEngine` (engine→insert→mixer) so the mixer + VU meters are genuinely live; a free-running internal transport so the TransportBar play/BPM are live. MIDI-PC preset hooks stubbed.
- **`mu_toni::PluginEditor : EditorShellBase`** — shared shell + `ChannelSidebar` (placeholder `LayerGlyph` mini-graphic) + `EnginePanel` + `MixerOverlay`. No bespoke shell/mixer code.
- **Standalone quit prompt** via the shared `mu_ui::confirmQuitAsync` ([Source/Plugin/StandaloneApp.cpp](Source/Plugin/StandaloneApp.cpp)).
- **`mu-toni-tests`** — shared global-FX APVTS layout coverage (mirrors mu-tant-tests; builds every default build).
- Presets: dirs + extensions wired (full = `.muToni`, per-layer = `.muLayer` — a neutral placeholder noun); save/load + the preset chrome are deferred until the engine defines what a preset contains.

### Current placeholders (revisit when the engine lands)

- **Fixed 4 "Layer" channels** (`kNumChannels`) — dynamic add/delete/reorder (the sidebar supports it; mu-clid/mu-tant wire `addVoice`/`removeVoice`) arrives with the engine. The sidebar Add button is currently inert.
- **`EnginePanel`** is a blank placeholder; the real engine + sequencer UI + the sidebar `createMiniVisual` graphic replace it.
- **`.muLayer`** per-layer extension is provisional — rename to the engine's real layer noun (camelCase) once defined.

### Conventions in force

- All product symbols under the `mu_toni::` namespace (boundary check `tests/scripts/check-core-boundary.py` catches mu-core regressions).
- `Source/` mirrors the family `{Plugin, Sequencer, UI, Persistence, License, Tests}` layout.
- Builds tick the shared family build counter (`add_dependencies(mu-toni increment_build_number)`).
