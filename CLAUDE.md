# CLAUDE.md

Family-shared guidance for Claude Code working in this monorepo. **Product-specific rules live under each plugin's own CLAUDE.md** ([mu-clid/CLAUDE.md](mu-clid/CLAUDE.md), [mu-tant/CLAUDE.md](mu-tant/CLAUDE.md), [mu-toni/CLAUDE.md](mu-toni/CLAUDE.md), [mu-on/CLAUDE.md](mu-on/CLAUDE.md)) — read the one matching the product you're working on, in addition to this file.

## Monorepo layout

```
mu-core/        Shared audio + FX + modulation + mixer UI + ProcessorBase + EditorShellBase (INTERFACE library)
mu-clid/        Euclidean rhythm sequencer + sample trigger plugin (VST3 + CLAP + Standalone + Lite)
mu-tant/        Wavetable drone synth — 8 voices, mixer, modulators, gate-pattern grid (VST3 + CLAP + Standalone)
mu-toni/        Scaffolding only — Source/{Plugin,Sequencer,UI,Persistence,License,Tests}/
mu-on/          909-style groove sequencer — Kick/Bass/Hat/Snare lanes, step grid, bass↔kick sidechain
docs/           Family-shared design docs; product-specific docs under docs/<product>/
tests/          Cross-plugin listening-test pipeline
```

The standard mu platform is everything in `mu-core`. New products link `mu-core` and supply their own sequencer/engine/UI under `<product>/Source/`. See [docs/design-plugin-family.md](docs/design-plugin-family.md) for the platform contract and engine swap-point pattern.

## Prerequisites

JUCE is not vendored. Set `JUCE_PATH` to a local JUCE checkout before configuring:

```powershell
$env:JUCE_PATH = "D:\JUCE"
```

## Build workflow

**Cardinal rule — build-number policy (owner rules, [cmake/IncrementBuildNumber.cmake](cmake/IncrementBuildNumber.cmake)):**

1. **A code change → a Debug build only.** Every Debug build **increments the number by exactly 1**, no matter how soon after the last one — there is no time/session throttle. Each Debug build is a distinct testable artefact with its own number.
2. **Release builds happen only when the owner explicitly says so.** A Release **rebuilds only the Release artefacts** (Debug artefacts are never touched) and is stamped with the **same number as the last Debug build** — Release **never** increments.
3. **Release ≤ last Debug, always.** At ship time Release equals the last Debug; as development continues the Debug counter marches ahead, so a previously-shipped Release legitimately sits *below* the current counter — that is normal. A Release coming out **higher** than the last Debug means the counter advanced without a Debug build → the build **stops with a FATAL_ERROR**; surface it to the owner, don't work around it.
4. **Every Release ships three ways:** (a) artefacts copied to the OneDrive tester share, (b) a zip built and uploaded to the GitHub "latest release" so the website download links resolve, **and** (c) **release notes promoted** — move the accumulated "Next release · In testing" items into a new dated `v1.0.NNN` section in each affected product's notes (`site/mu-clid-releases.html`, `site/mu-tant-releases.html`), clear the In-Testing section, and bump the hardcoded version default in `site/download.html`.

**Release notes on EVERY build (not just releases):** whenever a **Debug** build fixes or improves anything **user-facing**, add a plain-English one-liner — ending with the backlog item number in parens, e.g. "Fixed a rare crash when hot-swapping a rhythm (995)", grouped **New / Improved / Fixed**, **no low-level/internal detail** — to the **"Next release · In testing"** section of each affected product's release-notes page (a shared mu-core fix goes on every product whose code path it touches), then **commit + push the site** (site-only → Netlify auto-deploys, no CI). The In-Testing section is public and must always be current so users can see what's coming in the next version.
5. **No GitHub Actions workflow runs on push — ever.** `ci.yml` (Linux + Windows build + pluginval), `release.yml` (all-platform release), and `mac-validate.yml` (macOS) are all **`workflow_dispatch`-only**, so a `git push` of any path triggers nothing (GitHub minutes cost money). CI/release/mac-validate run **only** when the owner explicitly dispatches them — never as a side-effect of a build, commit, or push, and never re-add `push:`/`pull_request:` triggers.

```bash
cmake -B build                              # Configure (once, or after CMakeLists changes)
cmake --build build --config Debug          # Default for a change: Debug only, +1 to the build number
cmake --build build --config Release        # Owner-requested release: reuses the last Debug number
```

- **Always report the build number read from `mu-core/BuildNumber.h` *after* the final build** — never quote a number from an intermediate build log, or it will be stale.
- On a **Release** build configured with `-DMUFAMILY_DEPLOY_TESTERS=ON` (the `MUFAMILY_DEPLOY_TESTERS` option is **OFF by default**), **every product** deploys to the OneDrive tester share: mu-clid (+ Lite), mu-tant, mu-on, mu-toni, and mu-link's exe (each via a `MUFAMILY_DEPLOY_TESTERS`-guarded POST_BUILD). A plain Release with the flag off builds but stays local. Debug never deploys.
- Artefacts land at `build/<product>/<target>_artefacts/<Config>/<Format>/`. mu-clid/mu-clid-lite both use `build/mu-clid/...` because they share a CMakeLists.

**Plugin formats (family rule).** Every product builds **VST3 + Standalone + CLAP** on all platforms, **plus AU (Audio Unit v2) on macOS**. AU is macOS-only (needs Apple's AudioUnit SDK), so the root [CMakeLists.txt](CMakeLists.txt) defines `MUFAMILY_AU_FORMAT` (= `AU` on `APPLE`, empty otherwise) and **every `juce_add_plugin` must append `${MUFAMILY_AU_FORMAT}` to its `FORMATS`** — that's how a new sibling gets AU for free. (CLAP is added separately via `clap_juce_extensions_plugin`.) AU requires the shared `PLUGIN_MANUFACTURER_CODE TDP1` plus a **unique 4-char `PLUGIN_CODE`** per product. The `_AU` targets exist only in an `APPLE` configure, so they're built/validated on the macOS CI runner (`auval`), never locally on Windows. Shipping AU to Mac users later needs Apple notarization + signing (the Apple side of #99).

After every build, read [backlog.md](backlog.md) and fix open (unchecked) issues immediately, without asking, up to a maximum of 5 issues. Prioritise issues related to the current stage.

After every response, if any issues in `backlog.md` have changed status or new issues have been added, update `backlog.md` immediately to reflect the current state and ensure items are ordered correctly.

New feature ideas live in [docs/design-future.md](docs/design-future.md) under **Unscheduled Ideas**. Ask the user before implementing any of them.

## Git commit messages

Every commit message must include:
1. **Stage(s)** — which development stage(s) are included in this commit (e.g. `Stage 12`, `Stages 12–13`).
2. **Issues closed** — list each issue number and its one-line description (e.g. `Closes #12: rhythm rename propagation`).
3. **Full version** — `v1.0.<build>` using the current value from `build_number.txt`.

Example:
```
Stage 13: UI completions — Amp FX sends, intra-FX wiring verified, Settings Overlay audit

Closes #17: Amp FX send knobs (Effect/Delay/Reverb) added to Voice Amp row
Closes #22: Intra-FX APVTS wiring verified end-to-end
Closes #23: Settings Overlay audited against design spec

Version: v1.0.103
```

## Backlog handling

The backlog in `backlog.md` must always be grouped: **Open → On Hold → Fixed**. Within each group, items are ordered by issue number **descending** (highest first). Every backlog update must preserve this ordering. All code changes must be logged as backlog entries to maintain a complete development history.

## Family-shared design documents

| Sub-doc | When to read |
|---|---|
| [docs/design-plugin-family.md](docs/design-plugin-family.md) | **Shared plugin architecture** — `mu-core`, `ProcessorBase`, `EditorShellBase`, `VoiceSlot`, the platform contract / engine swap-point pattern. Read before structural / cross-plugin work. |
| [docs/design-ui-family.md](docs/design-ui-family.md) | **Shared design system** — colour tokens, typography, control sizes, interaction patterns, shared module plan. Read this before any UI work. |
| [docs/design-fx.md](docs/design-fx.md) | FX algorithms, delay, reverb, intra-FX routing, FXSlotBase interface. Lives in `mu-core/Audio/FX/`, used by all products. |
| [docs/design-future.md](docs/design-future.md) | Unscheduled future ideas — read to avoid closing off options during current stages. |
| [docs/DevelopmentHistory.md](docs/DevelopmentHistory.md) | Family-wide stage log (build numbers are shared across products). |

**Product-specific design docs** live under `docs/<product>/` and are linked from the matching product CLAUDE.md.

**Test catalogue:**

| Sub-doc | When to read |
|---|---|
| [tests.md](tests.md) | **Test catalogue + status** — listening tests, C++ unit tests, manual smoke plan. Pass/fail tracking lives here, not in backlog. |
| [tests/README.md](tests/README.md) | Listening-test pipeline mechanics — render flags, JSON schema, metric catalogue, adding-a-test recipe. |

## Critical architectural rules (family-wide)

These hold for everything in `mu-core` and every product that links it. Product-specific rules live in each product's CLAUDE.md.

- **Everything in APVTS** — if a parameter isn't in the ValueTree it won't save. All parameters wire through APVTS.
- **Audio thread never allocates** — all allocation in `prepareToPlay`, never in `processBlock`.
- **mu-core never depends on a plugin** — strictly one-way: plugins link mu-core, never the reverse. A `mu-core/**` file must not `#include` a plugin header or name a plugin symbol (`mu_clid::`, `mu_tant::`, …); mu-core knows only `ProcessorBase` + the shared interfaces. Enforced by `tests/scripts/check-core-boundary.py`. **When adding a file, decide its side first:** generic with no plugin-specific param IDs / semantics → `mu-core`; references product-specific concepts → that product's source tree. New plugin-specific code goes under the product's namespace (`mu_clid::`, `mu_tant::`) so the side is visible at every reference.
- **mu-core stays plugin-agnostic** — no `#include` of any product header from mu-core, no product-specific symbol names. Naming uses neutral terms (`channel` / `slot` / `layer` over `rhythm`). Use `MuLookAndFeel` directly from mu-core code, never the `MuClidLookAndFeel` back-compat alias.
- **ModulationMatrix is the single reader** — audio engine reads only from `ModulationMatrix`, never directly from APVTS or `ControlSequence`.
- **Channels are fully self-contained** — `ControlSequence`s may only target parameters within their own channel. No cross-channel modulation. Global FX parameters are not valid modulation destinations.
- **ControlSequence lengths are independent** — never couple loop lengths or rates to channel step counts.
- **ModulationMatrix processes in dependency order** — detects and rejects circular dependencies at assignment creation time.
- **FXSlotBase interface for all FX** — enables VST3 plugin hosting in v3 without refactoring.
- **TimeStretcherBase wraps the time-stretch engine** — currently a stub; SoundTouch (v1) or RubberBand (v2) slots in without refactoring.
- **Time-stretch DLL (SoundTouch/RubberBand) ships separately** — required for LGPL/GPL compliance when implemented.
- **All colours and sizes in MuLookAndFeel only** — no hardcoded values in component drawing code.
- **All UI uses the shared component library** — never build a one-off version of a standard control. The editor shell ([mu-core/UI/EditorShellBase.h](mu-core/UI/EditorShellBase.h)) supplies LookAndFeel, TransportBar, StatusBar, About / Save / Preset Browser / MIDI preset overlays, demo banner, overlay state machine, layout, keybindings — products supply only the sidebar + main panel + optional mixer / settings overlays.
- **Family consistency rule** — all project structuring (Source/ folder layout, docs/ topology, file extensions, naming conventions, ProcessorBase virtual hooks, backlog handling) must mirror across the mu-family (mu-clid / mu-tant / mu-toni / future siblings). Before adding a folder, naming a file, picking a convention for a new plugin, **mirror what the existing product does**. Before introducing a new convention for one product, **propagate it to the others**. Concrete current conventions: per-layer preset = camelCase noun (`.muRhythm`, `.muPattern`); full preset = plugin-name camelCase (`.muClid`, `.muTant`); Source subfolders = `{License, Persistence, Plugin, Sequencer, Tests, UI}`; product docs at `docs/<product>/` mirroring mu-clid's set as topics get decided.

## Code style (mandatory)

- **No backlog issue numbers in comments.** Writing `// #123`, `// fix for #123`, `// added in #xxx`, or any other backlog reference in source code is forbidden. Backlog context belongs in commit messages and PR descriptions. Comments rot out of sync with the backlog and a stale `#NNN` reference is worse than no reference.
- **Comments must help Andy read and understand the code.** Concise, clear, and focused on the *why* and *what* (not the *how*, which the code itself shows).
  - **Loops** — comment the purpose of the loop. What is it doing as a whole? (`// Apply per-voice modulation across all active rhythms.`)
  - **Algorithms** — comment what the algorithm does, and cite the source if it's not obvious (`// ADAA tanh — Reiss & Stefanidis 2016`, `// Signalsmith FDN reverb`, `// Karplus-Strong delay-line feedback loop`). A reader should be able to look up the reference if they want to dig deeper.
  - **Section headers** — when a function contains clearly separate phases, put a one-line comment at the top of each phase naming its purpose and result (`// Phase 1: gather analysis frames → spectrum[]`).
- One sentence per comment is almost always enough. If you need more, the code probably wants to be split into named helpers instead.

## Key family-wide patterns

### KnobWithLabel callbacks
[KnobWithLabel](mu-core/UI/Components/KnobWithLabel.h) has **two** separate callbacks:
- `onStatusUpdate(name, valueString)` — called automatically from the internal `slider.onValueChange` for status bar display.
- `onValueChanged(double)` — also called from the same `slider.onValueChange` lambda; use this for data mutation.

Never override `getSlider().onValueChange` directly — it replaces both callbacks. Always use `onValueChanged` for data binding, or attach via `juce::AudioProcessorValueTreeState::SliderAttachment`.

## Third-party libraries

| Library | Purpose | Notes |
|---|---|---|
| JUCE | Core framework | Via `JUCE_PATH` env var |
| Signalsmith Reverb | Room/hall/plate reverb | MIT, header-only |
| Monocypher | License key crypto | BSD-2-Clause, compiled in |
| clap-juce-extensions | CLAP format support | MIT, compiled in |
| SoundTouch | Time stretching (v1, planned) | LGPL — will ship as DLL when implemented |
| RubberBand | Time stretching (v2, planned) | Wrapped behind `TimeStretcherBase` — no refactor needed when upgrading |

## UI values

Knob colour coding, window sizing, and all layout constants are defined in [mu-core/UI/Components/MuLookAndFeel.h](mu-core/UI/Components/MuLookAndFeel.h). Family-wide design notes in [docs/design-ui-family.md](docs/design-ui-family.md); product-specific layouts in `docs/<product>/design-ui.md`.

## Development history

mu-clid Stages 1–34 are complete; see [docs/DevelopmentHistory.md](docs/DevelopmentHistory.md) for the stage-by-stage log. Post-Stage-34 work (the mu-core shell lift, the mu-tant build-out) is tracked as numbered [backlog.md](backlog.md) issues rather than staged — the "stage" framework was mu-clid's v1 roadmap and isn't used for cross-family / mu-tant work. Log every code change as a backlog entry (see Backlog handling).
