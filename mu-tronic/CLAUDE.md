# mu-Tronic — CLAUDE.md

Product-specific guidance for working in `mu-tronic/`. Read alongside the family-wide [/CLAUDE.md](../CLAUDE.md).

## Project overview

**μ-Tronic** is the family's performance unit: **16 layer slots** (paged 1–8 / 9–16), each holding a layer taken from another product, on the shared modulators, insert effects and mixer. It adds **three group buses with two inserts each**, **scenes** (clip *n* launched on every layer at once) and MIDI in / out. **Standalone only** (no VST3 / CLAP / AU); the shell is `EditorShellBase` like every other product.

Design: [docs/mu-tronic/design.md](../docs/mu-tronic/design.md) (architecture ruling), [design-ui.md](../docs/mu-tronic/design-ui.md), [extraction-plan.md](../docs/mu-tronic/extraction-plan.md) (how the other products' engines became the `mu-<product>-layer` libraries).

## Release status

A live release product (backlog #1678), released with the rest of the series: **pay what you want**, same terms as mu-Clid / mu-Tant / mu-Toni / mu-On; Windows Standalone, Linux in beta, macOS on hold. Version base and build number are the family's (`version-pre.txt` carries the same base as `version.txt`; the shared build number never resets). Manual: [docs/mu-tronic/create_manual.ps1](../docs/mu-tronic/create_manual.ps1) (edit the script, not the `.docx`). Site pages: `site/mu-tronic.html`, `site/mu-tronic-releases.html`.

**Licensing is not wired yet.** The design ruling of 2026-10-15 made mu-Tronic freeware with no key code of its own; the owner then chose pay-what-you-want (2026-10-27), which needs a `License/LicenseKey.h`, the shared activation / support-prompt flow and a Lemon Squeezy product. Until that lands `Source/License/` is empty and the app runs unlimited with no prompt.

## Layer types

Static layer libraries compiled into the binary, registered in [Source/Persistence/LayerTypes.cpp](Source/Persistence/LayerTypes.cpp): Arp (`mu-toni-layer`), Kick / Bass / Hat / Snare / Rumble / FM Perc (`mu-on-layer`), Pattern (`mu-tant-layer`), Rhythm (`mu-clid-layer`). Engines do not update independently: suite-wide releases, and mu-Tronic is rebuilt and released whenever an app's engine changes.

## Rules

- A layer library never includes `Plugin/` or `License/` (`tests/scripts/check-layer-boundary.ps1`); mu-Tronic-only code lives in `mu_tronic::` under `Source/`.
- Capacities are per-target compile definitions (`MU_MAX_LAYERS=16`, `MU_MAX_BUSES=3`, `MU_BUS_INSERTS=2`); the other products keep 8 layers and no buses. No code may assume 8 or 2 literally (`kLayersPerPage`).
- Layer state is text, `RenderContext` mirrors a C struct, `render` makes no host callback (keeps a module ABI possible, design section 2).
- Tests: `mu-tronic-tests` (builds every default build).
