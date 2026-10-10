# CLAUDE.md

Family-shared guidance for Claude Code working in this monorepo. **Product-specific rules live under each plugin's own CLAUDE.md** ([mu-clid/CLAUDE.md](mu-clid/CLAUDE.md), [mu-tant/CLAUDE.md](mu-tant/CLAUDE.md), [mu-toni/CLAUDE.md](mu-toni/CLAUDE.md), [mu-on/CLAUDE.md](mu-on/CLAUDE.md)) — read the one matching the product you're working on, in addition to this file.

## Monorepo layout

```
mu-core/        Shared audio + FX + modulation + mixer UI + ProcessorBase + EditorShellBase (INTERFACE library)
mu-control/     (planned, created with the first controller work) Controller drivers, mapping and surface model; depends on mu-core, never the reverse
mu-clid/        Euclidean rhythm sequencer + sample trigger plugin (VST3 + CLAP + Standalone + Lite)
mu-tant/        Wavetable drone synth — 8 voices, mixer, modulators, gate-pattern grid (VST3 + CLAP + Standalone)
mu-toni/        Generative arpeggiator mono-synth — scale/chord note pool, skewed-triangle scan, freeware (VST3 + CLAP + Standalone)
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

**JUCE is kept up to date (owner rule, 2026-10-09).** The family tracks the latest stable JUCE release rather than staying on an old one. The version is pinned in **one place**, [juce-version.txt](juce-version.txt): every CI workflow checks out that tag and configure fails if the local `JUCE_PATH` checkout differs, so a bump is a one-line edit and local builds cannot drift from CI. To bump: read JUCE's `BREAKING_CHANGES.md` between the old and new version, grep the tree for each affected API, bump the pin, then run the unit tests, the round-trip listening tests and pluginval on the build PC. When writing code, use current JUCE APIs and do not build on deprecated ones. Version in use: JUCE 9.0.3 today. `pwsh tests/scripts/check-juce-version.ps1` reports how far the pin is behind the latest release.

## Build workflow

**Procedure lives in the `/build` and `/release` skills** ([.claude/commands/build.md](.claude/commands/build.md), [.claude/commands/release.md](.claude/commands/release.md)); full reference detail (plugin formats, deploy, artefact paths, three-way ship) is in [docs/design-plugin-family.md](docs/design-plugin-family.md#build--packaging-reference). The invariants that hold on every build:

- **Always reconfigure first** (`cmake -B build`) — generated files (icon, versioninfo.rc, build-number bake) go stale otherwise.
- **A code change → a Debug build only**, all products. Every Debug build **increments the number by exactly 1**. **Never bump `build_number.txt` manually.**
- **Release builds only when the owner says so** — reuses the last Debug number, never increments; **Release ≤ last Debug** (a higher Release aborts with FATAL_ERROR — surface it, don't work around).
- **Report the number read from `mu-core/BuildNumber.h` *after* the final build** — never an intermediate log value.
- **Version = `<major>.<minor>` from [version.txt](version.txt) + the build number** — currently 1.1 (owner moved the family to 1.1 at the UI rework, build 972). Plugins/tags show `v1.1.0.NNN`, commits `v1.1.NNN`. The build number never resets on a major/minor bump (the Release ≤ Debug guard and the in-plugin update check rely on it). Change major/minor **only** by editing `version.txt`, and only when the owner says so.
- **No GitHub Actions workflow runs on push — ever.** All workflows are `workflow_dispatch`-only; never re-add `push:`/`pull_request:` triggers, and nothing runs as a side-effect of a plain build/commit/push. **`release.yml` is the exception that IS dispatched as a standard step of every release** (`/release` step b) — the repo is public so Linux CI minutes are free, so a release always publishes a full cross-platform (Windows + Linux) GitHub release. `ci.yml` + `mac-validate.yml` still run only on explicit owner request.
- **macOS is on hold for the foreseeable future** (owner decision, 2026-10-02) — without an Apple code-signing certificate the builds can't be notarized, so nothing ships for Mac. Keep the existing `APPLE`/AU code paths compiling-in-principle (don't delete them), but don't build, ship, advertise or spend effort on macOS until the owner lifts the hold.

**Release notes on EVERY build (not just releases):** whenever a build fixes or improves anything **user-facing**, run `/notes` ([.claude/commands/notes.md](.claude/commands/notes.md)) to add a plain-English one-liner to the **"Next release · In testing"** section of each affected product's release-notes page, then commit + push the site. The In-Testing section is public and must always be current so users can see what's coming in the next version.

**The `backlog-administrator` agent ([.claude/agents/backlog-administrator.md](.claude/agents/backlog-administrator.md)) is the only writer of [backlog.md](backlog.md)** (owner rule, 2026-10-09). Nothing else — the main conversation, other agents, slash commands, hooks — edits it directly; they hand the change to the agent, which applies the ordering and format rules and runs `check-backlog.ps1`. It also hands tasks out: ask it for the next task (or a named issue) and it returns a brief with the steps, files, docs to read, blockers and done criteria.

After every build, ask the `backlog-administrator` for the open issues and fix them immediately, without asking, up to a maximum of 5 issues. Prioritise issues related to the current stage.

After every response, if any issues in `backlog.md` have changed status or new issues have been added, hand the changes to the `backlog-administrator` immediately so the file reflects the current state and stays correctly ordered.

New feature ideas live in [docs/design-future.md](docs/design-future.md) under **Unscheduled Ideas**. Ask the user before implementing any of them.

**Standing check on ALL work (owner rule):** before and while making any change — code, design, docs, backlog — read [docs/design-future.md](docs/design-future.md) and ask *"am I making these ideas easier or harder?"* — in particular the one-framework / one-instance direction (shared `mu-core`, only the sound engine and sequencer differ per product) and the standard UI sizing. Prefer the change that moves toward them (generic code in `mu-core`, no new per-product copies of shared behaviour, no new per-product layout constants); if a change must make one harder, say so and why before doing it.

**The `user-documentation-author` agent ([.claude/agents/user-documentation-author.md](.claude/agents/user-documentation-author.md)) owns the user manuals** (`docs/<product>/create_manual.ps1` and the `.docx` it generates) and keeps them current (owner rule, 2026-10-09). After any user-facing change, hand it what changed so the manual is updated in the same commit as the code. It is separate from `/notes`, which owns the release-notes pages.

**The `test-steward` agent ([.claude/agents/test-steward.md](.claude/agents/test-steward.md)) owns testing** — unit tests, the listening-test pipeline, [tests.md](tests.md) and pass/fail status — and writes the tests for fixes and for DSP findings; it never changes product source. **The `architecture-steward` agent ([.claude/agents/architecture-steward.md](.claude/agents/architecture-steward.md)) owns platform structure** — what goes in `mu-core` vs a product vs `mu-control`, naming, folder layout, the family docs and the standing `design-future` check — and rules before structural or cross-plugin work (owner rule, 2026-10-09).

### Agent precedence

Six agents each own one area and are the final word in it; none edits another's files:

| Agent | Final say on | Writes |
|---|---|---|
| `backlog-administrator` | `backlog.md` content, order, status; task hand-over | `backlog.md` only |
| `ux-controller` | look, feel, design docs | UI design docs |
| `architecture-steward` | structure, naming, placement, family docs | architecture and naming docs |
| `dsp-controller` | real-time safety and DSP correctness (a **Blocked** verdict stops the change) | nothing (read-only) |
| `test-steward` | what is tested and its recorded status | tests, `tests/**`, `tests.md` |
| `user-documentation-author` | user manuals | manual scripts and `.docx` |
| main conversation | the code, and the owner's instructions | source |

**Call the agents without asking** (owner rule, 2026-10-09). The main conversation invokes them on its own whenever their trigger applies, and does not ask the owner for permission first:

- `backlog-administrator`: any backlog change, any task hand-over, after every build and response (batch the changes into one call).
- `ux-controller`: before UI work and for any design question the docs don't settle.
- `architecture-steward`: before structural, cross-plugin or naming decisions.
- `dsp-controller`: on every new or changed audio-path code, before building or committing.
- `test-steward`: when a fix, feature or DSP finding needs a test, and before a release.
- `user-documentation-author`: after any user-facing change.

Run independent agents in parallel. Report their outcomes to the owner briefly; only owner decisions an agent hands up are asked about.

On a conflict: the owner overrides everything; the `dsp-controller` wins on anything that runs on the audio thread; the `architecture-steward` wins on where code lives and what it is called; the `ux-controller` wins on what the user sees. Anything an agent finds outside its own area goes to the main conversation, which hands it to the owner of that area.

## Git commit messages

Every commit message must include three things: **Stage(s)** (e.g. `Stage 12`), **Issues closed** (each number + one-line description, e.g. `Closes #12: rhythm rename propagation`), and **Full version** (`v1.1.<build>` from `build_number.txt`). Example:

```
Stage 13: UI completions — Amp FX sends, intra-FX wiring verified

Closes #17: Amp FX send knobs added to Voice Amp row
Closes #22: Intra-FX APVTS wiring verified end-to-end

Version: v1.1.972
```

## Backlog handling

The backlog in `backlog.md` must always be grouped: **Open → On Hold → Closed**. Within **On Hold** and **Closed**, items are ordered by issue number **descending** (highest first). **Open is in recommended development order** (owner rule, 2026-10-09): work from the top; a new item goes on the top until it is ranked into its place, and the note under the Open heading names the stages. Every backlog update must preserve this ordering. All code changes must be logged as backlog entries to maintain a complete development history.

The single resolved status is **`✅ Closed`** — never `Fixed` / `Audited` / `Verified` / `Done` or any other variant, whether the item was fixed, verified as already correct, or resolved by design. Structure is enforced by [tests/scripts/check-backlog.ps1](tests/scripts/check-backlog.ps1); run it after editing the backlog.

## Family-shared design documents

| Sub-doc | When to read |
|---|---|
| [docs/design-plugin-family.md](docs/design-plugin-family.md) | **Shared plugin architecture** — `mu-core`, `ProcessorBase`, `EditorShellBase`, `VoiceSlot`, the platform contract / engine swap-point pattern. Read before structural / cross-plugin work. |
| [docs/design-ui-family.md](docs/design-ui-family.md) | **Shared design system** — colour tokens, typography, control sizes, interaction patterns, shared module plan. Read this before any UI work. |
| [docs/design-ableton-link.md](docs/design-ableton-link.md) | **Ableton Link plan** — why, the licence decision that gates it, where it fits (mu-link as the peer, a resolver source), staged work, and the sync groundwork that must come first. |
| [docs/design-launchpad.md](docs/design-launchpad.md) | **Launchpad X controller plan** and where the controller code lives (`mu-control`) — the device's MIDI implementation (read from Novation's manual), a proposed live layout, the risks to check on hardware, and the order of work. Read before any MIDI-input mapping or controller-output work. |
| [docs/design-naming.md](docs/design-naming.md) | **Naming conventions** — the Layer vocabulary and parent class, namespaces, class-name roles, folders, identifier style, and what is still out of line. Read before naming a class, file, folder or parameter. |
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

**The `dsp-controller` agent ([.claude/agents/dsp-controller.md](.claude/agents/dsp-controller.md)) reviews every DSP / audio-thread implementation** (owner rule, 2026-10-09) for efficiency, real-time safety, race conditions and other errors. Run it on any new or changed DSP code before it is built or committed; a **Blocked** verdict must be fixed first. It is read-only: it reports, the main conversation fixes.

These hold for everything in `mu-core` and every product that links it. Product-specific rules live in each product's CLAUDE.md.

- **Everything in APVTS** — if a parameter isn't in the ValueTree it won't save. All parameters wire through APVTS.
- **Audio thread never allocates** — all allocation in `prepareToPlay`, never in `processBlock`.
- **mu-core never depends on a plugin** — strictly one-way: plugins link mu-core, never the reverse. The planned `mu-control` library (controller drivers) follows the same rule and `mu-core` must never include it. A `mu-core/**` file must not `#include` a plugin header or name a plugin symbol (`mu_clid::`, `mu_tant::`, …); mu-core knows only `ProcessorBase` + the shared interfaces. Enforced by `tests/scripts/check-core-boundary.ps1`. **When adding a file, decide its side first:** generic with no plugin-specific param IDs / semantics → `mu-core`; references product-specific concepts → that product's source tree. New plugin-specific code goes under the product's namespace (`mu_clid::`, `mu_tant::`) so the side is visible at every reference.
- **mu-core stays plugin-agnostic** — no `#include` of any product header from mu-core, no product-specific symbol names. Naming uses neutral terms (`channel` / `slot` / `layer` over `rhythm`). Use `MuLookAndFeel` directly from mu-core code, never the `MuClidLookAndFeel` back-compat alias.
- **Modulation depth = % of the target knob's range** — every product defines its modulation targets in one table of `mu_mod::ModTarget` rows (id, label, section, parameter); a new target is one appended row, never a hand-set scale. Rules + per-product tables in [docs/design-plugin-family.md](docs/design-plugin-family.md#modulation-targets--family-standard); enforced by `mu_mod::checks` in each product's tests and `tests/scripts/check-mod-targets.ps1`.
- **ModulationMatrix is the single reader** — audio engine reads only from `ModulationMatrix`, never directly from APVTS or `ControlSequence`.
- **Channels are fully self-contained** — `ControlSequence`s may only target parameters within their own channel. No cross-channel modulation. Global FX parameters are not valid modulation destinations.
- **ControlSequence lengths are independent** — never couple loop lengths or rates to channel step counts.
- **ModulationMatrix processes in dependency order** — detects and rejects circular dependencies at assignment creation time.
- **FXSlotBase interface for all FX** — enables VST3 plugin hosting in v3 without refactoring.
- **TimeStretcherBase wraps the time-stretch engine** — currently a stub; SoundTouch (v1) or RubberBand (v2) slots in without refactoring.
- **Time-stretch DLL (SoundTouch/RubberBand) ships separately** — required for LGPL/GPL compliance when implemented.
- **All colours and sizes in MuLookAndFeel only** — no hardcoded values in component drawing code.
- **All UI uses the shared component library** — never build a one-off version of a standard control. The editor shell ([mu-core/UI/EditorShellBase.h](mu-core/UI/EditorShellBase.h)) supplies LookAndFeel, TransportBar, StatusBar, About / Save / Preset Browser / MIDI preset overlays, demo banner, overlay state machine, layout, keybindings — products supply only the sidebar + main panel + optional mixer / settings overlays.
- **Family consistency rule** — all project structuring (Source/ folder layout, docs/ topology, file extensions, naming conventions, ProcessorBase virtual hooks, backlog handling) must mirror across the mu-family (mu-clid / mu-tant / mu-toni / future siblings). Before adding a folder, naming a file, picking a convention for a new plugin, **mirror what the existing product does**. Before introducing a new convention for one product, **propagate it to the others**. Concrete current conventions: per-layer preset = camelCase noun (`.muRhythm`, `.muPattern`; layer types are `Rhythm` / `Pattern` / `Arp` / `Track` for mu-Clid / mu-Tant / mu-Toni / mu-On); full preset = plugin-name camelCase (`.muClid`, `.muTant`); Source subfolders = **the same eight in every instrument product, empty ones kept with a `.gitkeep`**: `{Audio, License, Modulation, Persistence, Plugin, Sequencer, Tests, UI}` (see [docs/design-naming.md](docs/design-naming.md)); product docs at `docs/<product>/` mirroring mu-clid's set as topics get decided.

## Code style (mandatory)

- **No backlog issue numbers in comments.** Enforced by `tests/scripts/check-comment-refs.ps1` (run as step 0 of `/build`; `ref-ok` in the comment marks a legitimate `#N`). Writing `// #123`, `// fix for #123`, `// added in #xxx`, or any other backlog reference in source code is forbidden. Backlog context belongs in commit messages and PR descriptions. Comments rot out of sync with the backlog and a stale `#NNN` reference is worse than no reference.
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

JUCE (via `JUCE_PATH`), Signalsmith Reverb, Monocypher, clap-juce-extensions, and the planned SoundTouch/RubberBand time-stretch engines — full table with licences in [docs/design-plugin-family.md](docs/design-plugin-family.md#third-party-libraries).

## UI values

**The `ux-controller` agent ([.claude/agents/ux-controller.md](.claude/agents/ux-controller.md)) owns look, feel and design and all the design documentation** (owner rule, 2026-10-09). It arbitrates every design choice the coding needs: before UI work, or when a design question comes up that the docs don't settle, ask it for a ruling (it returns the tokens, sizes, components and states to use, and records the decision in the docs). Design docs ([docs/design-ui-family.md](docs/design-ui-family.md), `docs/<product>/design-ui.md`) are edited by it, not ad hoc. It does not write code or edit the backlog.

Knob colour coding, window sizing, and all layout constants are defined in [mu-core/UI/Components/MuLookAndFeel.h](mu-core/UI/Components/MuLookAndFeel.h). **Every shadow, highlight and tint strength lives in `MuTheme::Lighting`** ([mu-core/UI/Components/MuTheme.h](mu-core/UI/Components/MuTheme.h)) — with master `shadowAmount` / `highlightAmount` that scale them all for every product. Never hard-code an alpha for depth in drawing code; add a field there. **The metal style is the family-standard look** — every app turns it on with one `setMetalStyle(true[, appAccent])` call at the end of its editor constructor; the elements (metal panels, raised boxes, name plates, LCD selectors, lamps, engraved labels) and their rules are in [docs/design-ui-family.md §11](docs/design-ui-family.md#11-metal-style-family-standard). Build new UI from those mu-core helpers, never one-off drawing. Family-wide design notes in [docs/design-ui-family.md](docs/design-ui-family.md); product-specific layouts in `docs/<product>/design-ui.md`.

## Development history

mu-clid Stages 1–34 are complete; see [docs/DevelopmentHistory.md](docs/DevelopmentHistory.md) for the stage-by-stage log. Post-Stage-34 work (the mu-core shell lift, the mu-tant build-out) is tracked as numbered [backlog.md](backlog.md) issues rather than staged — the "stage" framework was mu-clid's v1 roadmap and isn't used for cross-family / mu-tant work. Log every code change as a backlog entry (see Backlog handling).
