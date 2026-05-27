# mu-Toni — CLAUDE.md

Product-specific guidance for working in `mu-toni/`. Read alongside the family-wide [/CLAUDE.md](../CLAUDE.md).

## Status

**Scaffolding only.** No `juce_add_plugin` target, no real source. The `Source/` subtree mirrors mu-clid's `{Plugin, Sequencer, UI, Persistence, License, Tests}` layout as `.gitkeep` placeholders so the family consistency rule is respected from day one.

## When real source lands

1. Copy the `juce_add_plugin / target_sources / target_link_libraries` pattern from [mu-tant/CMakeLists.txt](../mu-tant/CMakeLists.txt) (the smallest current example).
2. Uncomment `add_subdirectory(mu-toni)` in the root [CMakeLists.txt](../CMakeLists.txt).
3. PluginProcessor extends `ProcessorBase`; PluginEditor extends `EditorShellBase` (from `mu-core/UI/EditorShellBase.h`).
4. All product symbols live under the `mu_toni::` namespace so the boundary check (`tests/scripts/check-core-boundary.py`) catches accidental mu-core regressions.
5. File extensions per family rule: per-layer preset = camelCase noun, full preset = `.muToni`.
6. Add `add_dependencies(mu-toni increment_build_number)` so mu-toni builds tick the shared family build counter.
