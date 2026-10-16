# CLAUDE.md

Family-shared guidance for Claude Code working in this monorepo. **Product-specific rules live under each plugin's own CLAUDE.md** ([mu-clid/CLAUDE.md](mu-clid/CLAUDE.md), [mu-clid-midi/CLAUDE.md](mu-clid-midi/CLAUDE.md), [mu-tant/CLAUDE.md](mu-tant/CLAUDE.md), [mu-toni/CLAUDE.md](mu-toni/CLAUDE.md), [mu-on/CLAUDE.md](mu-on/CLAUDE.md)) — read the one matching the product you're working on, in addition to this file.

## Monorepo layout

```
mu-core/        Shared audio + FX + modulation + mixer UI + ProcessorBase + EditorShellBase (INTERFACE library)
mu-control/     (planned, created with the first controller work) Controller drivers, mapping and surface model; depends on mu-core, never the reverse
mu-clid/        Euclidean rhythm sequencer + sample trigger plugin (VST3 + CLAP + Standalone)
mu-clid-midi/   mu-Clid MIDI: Euclidean MIDI effect, the former mu-Clid Lite — no audio, free, VST3 + CLAP; shares Rhythm / .muRhythm with mu-Clid (see mu-clid-midi/CLAUDE.md)
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

**JUCE is kept current** (owner rule, 2026-10-09): one pin in [juce-version.txt](juce-version.txt) that CMake and every CI workflow read; use current JUCE APIs and never build on deprecated ones. Bump procedure and the check script: [docs/design-plugin-family.md](docs/design-plugin-family.md#juce-version-policy).

## How a round works

1. **Take work.** Ask the `backlog-administrator` for the next task(s) from the top of Open; it returns a brief.
2. **Prepare.** Once per batch of related work, get rulings from `ux-controller` / `architecture-steward` where the docs don't settle it, and do the `design-future` standing check.
3. **Code.** Make the changes; run the static checks (`tests/scripts/check-*.ps1`).
4. **Review the batch.** `dsp-controller` and any other applicable reviewer review the whole batch once. Their findings go onto the existing backlog row (via the `backlog-administrator`); fix them; each reviewer re-checks and answers Pass.
5. **Build when it is worth it** (see "Keep working until a build is worth it"), say in one line why, and report the number read from `mu-core/BuildNumber.h`.
6. **Follow up.** `test-steward` for tests, `user-documentation-author` for the manuals, `/notes` for release notes: each once for the batch.
7. **Close and commit.** Items with every applicable sign-off and build evidence are closed by the `backlog-administrator`; commit with `Closes` (signed off) or `Refs` (not yet) and push. Then back to step 1.
## Build workflow

**Procedure lives in the `/build` and `/release` skills** ([.claude/commands/build.md](.claude/commands/build.md), [.claude/commands/release.md](.claude/commands/release.md)); full reference detail (plugin formats, deploy, artefact paths, three-way ship) is in [docs/design-plugin-family.md](docs/design-plugin-family.md#build--packaging-reference). The invariants that hold on every build:

- **Always reconfigure first** (`cmake -B build`) — generated files (icon, versioninfo.rc, build-number bake) go stale otherwise.
- **When a build is wanted → a Debug build only** (see "Keep working until a build is worth it" below), all products. Every full Debug build **increments the number by exactly 1**. While working on **one or two apps**, `-DMUFAMILY_FOCUSED_BUILD=ON` builds just those targets without incrementing the number (owner, 2026-10-12); the full Debug build of all products at a stage boundary is the one that increments it. **Never bump `build_number.txt` manually.**
- **Release builds only when the owner says so** — reuses the last Debug number, never increments; **Release ≤ last Debug** (a higher Release aborts with FATAL_ERROR — surface it, don't work around).
- **Report the number read from `mu-core/BuildNumber.h` *after* the final build** — never an intermediate log value.
- **Version = `<major>.<minor>` + the shared build number.** Live products use [version.txt](version.txt), pre-release products (mu-On, mu-Toni) use [version-pre.txt](version-pre.txt); change a base only by editing its file and only when Andy says so, and the build number never resets. Details: [docs/design-plugin-family.md](docs/design-plugin-family.md#version-numbering).
- **No GitHub Actions workflow runs on push — ever.** All workflows are `workflow_dispatch`-only; never re-add `push:`/`pull_request:` triggers. `release.yml` is dispatched as a standard step of every release (`/release` step b); `ci.yml` and `mac-validate.yml` run only on explicit owner request.
- **macOS is on hold** (owner, 2026-10-02): no Apple signing certificate, so nothing ships for Mac. Keep the `APPLE`/AU code paths compiling-in-principle; don't build, ship or advertise macOS until the owner lifts the hold.

**Release notes per batch of user-facing changes:** whenever the work since the last `/notes` fixes or improves anything **user-facing**, run `/notes` once for the whole batch ([.claude/commands/notes.md](.claude/commands/notes.md)) to add a plain-English one-liner to the **"Next release · In testing"** section of each affected product's release-notes page, then commit + push the site. The In-Testing section is public and must always be current so users can see what's coming in the next version.

**The `backlog-administrator` agent ([.claude/agents/backlog-administrator.md](.claude/agents/backlog-administrator.md)) is the only writer of [backlog.md](backlog.md)** (owner rule, 2026-10-09). Nothing else — the main conversation, other agents, slash commands, hooks — edits it directly; they hand the change to the agent, which applies the ordering and format rules and runs `check-backlog.ps1`. It also hands tasks out: ask it for the next task (or a named issue) and it returns a brief with the steps, files, docs to read, blockers and done criteria.

**Keep working until a build is worth it** (owner rule, 2026-10-12; replaces the fixed issue cap). After a build, ask the `backlog-administrator` for the next task and keep fixing issues from the top of Open without asking, prioritising the current stage. There is no fixed number per round: stop and build when a build is the sensible next step, which is when one of these is true: (1) the next change can't be checked or built on without it, (2) a group of related changes is complete and ready to verify together, (3) the risk of an unbuilt compile error or a hard-to-find failure has grown (many files, `mu-core` touched, several products affected), (4) the change must be heard or seen to judge it, or (5) the owner asks. Don't build after each small fix just because it's finished, and don't keep going past the point where a failed build would be hard to trace. Say in one line why you are building.

When issues in `backlog.md` change status or new ones are needed, hand the changes to the `backlog-administrator` as one batched call at the end of the round (not after every small step), so the file reflects the current state and stays correctly ordered.

New feature ideas live in [docs/design-future.md](docs/design-future.md) under **Unscheduled Ideas**. Ask the user before implementing any of them.

**Standing check on ALL work (owner rule):** before and while making any change — code, design, docs, backlog — read [docs/design-future.md](docs/design-future.md) (once per session or batch of related work, and again whenever the change is structural) and ask *"am I making these ideas easier or harder?"* — in particular the one-framework / one-instance direction (shared `mu-core`, only the sound engine and sequencer differ per product) and the standard UI sizing. Prefer the change that moves toward them (generic code in `mu-core`, no new per-product copies of shared behaviour, no new per-product layout constants); if a change must make one harder, say so and why before doing it.

### Agent precedence

Seven agents each own one area and are the final word in it; none edits another's files:

| Agent | Final say on | Writes |
|---|---|---|
| `backlog-administrator` | `backlog.md` content, order, status; task hand-over | `backlog.md` only |
| `ux-controller` | look, feel, design docs | UI design docs |
| `architecture-steward` | structure, naming, placement, family docs | architecture and naming docs |
| `dsp-controller` | real-time safety and DSP correctness (a **Blocked** verdict stops the change) | nothing (read-only) |
| `test-steward` | what is tested and its recorded status | tests, `tests/**`, `tests.md` |
| `user-documentation-author` | user manuals | manual scripts and `.docx` |
| `web-designer` | the website, its copy and release-notes pages | `site/**` |
| main conversation | the code, and the owner's instructions | source |

**Call the agents without asking** (owner rule, 2026-10-09). The main conversation invokes them on its own whenever their trigger applies, and does not ask the owner for permission first:

Each agent's file says when to use it. Calls are once per batch of related work, not per small change; `dsp-controller` reviews before the build or commit that covers a batch, and a **Blocked** verdict is fixed first. `backlog-administrator` gets one batched call at the end of a round.

**Reviews update the item, they don't spawn new ones** (owner rule, 2026-10-12), for all reviewers (`dsp-controller`, `architecture-steward`, `test-steward`, `ux-controller`, `user-documentation-author`): when a change belongs to a backlog issue, the main conversation passes each reviewer's findings to the `backlog-administrator`, who records them on that row; the fixes are made; each reviewer re-checks and answers Pass; and only when every applicable reviewer has passed does the main conversation ask the `backlog-administrator` to close the row. A genuinely separate defect still gets its own row. A review never triggers another review.

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

The backlog in `backlog.md` must always be grouped: **Open → On Hold → Closed**. Within **On Hold** and **Closed**, items are ordered by issue number **descending** (highest first). **Open is in recommended development order** (owner rule, 2026-10-09): work from the top; a new item goes on the top until it is ranked into its place, and the note under the Open heading names the stages. Every backlog update must preserve this ordering. A backlog row is for owner-visible work, defects and Risks; a small change that belongs to an open item is recorded on that item's row, and a trivial internal tidy-up needs only its commit message (the git log is the full development history).

**Open rows are short and carry a few fixed fields** (priority P1/P2/P3, who logged it, details, dated updates, who still has to sign off); the exact format and rules are in [.claude/agents/backlog-administrator.md](.claude/agents/backlog-administrator.md). Review findings go on the item's row, not into new rows.

The single resolved status is **`✅ Closed`** — never `Fixed` / `Audited` / `Verified` / `Done` or any other variant, whether the item was fixed, verified as already correct, or resolved by design. Structure is enforced by [tests/scripts/check-backlog.ps1](tests/scripts/check-backlog.ps1); run it with `pwsh` after editing the backlog, and use its `-FixTally` switch instead of hand-editing the tally line. **Two PCs edit the backlog** (owner, 2026-10-16): `git pull --rebase` before any backlog edit, take a new number only after that pull, commit and push straight after; the laptop logs, rates and ranks, the build PC closes rows with build evidence. The full procedure is in [.claude/agents/backlog-administrator.md](.claude/agents/backlog-administrator.md).

## Family-shared design documents

| Sub-doc | When to read |
|---|---|
| [docs/design-plugin-family.md](docs/design-plugin-family.md) | **Shared plugin architecture** — `mu-core`, `ProcessorBase`, `EditorShellBase`, `VoiceSlot`, the platform contract / engine swap-point pattern. Read before structural / cross-plugin work. |
| [docs/design-ui-family.md](docs/design-ui-family.md) | **Shared design system** — colour tokens, typography, control sizes, interaction patterns, shared module plan. Read this before any UI work. |
| [docs/design-ableton-link.md](docs/design-ableton-link.md) | **Ableton Link plan** — the licence decision that gates it, where it fits, staged work. |
| [docs/design-launchpad.md](docs/design-launchpad.md) | **Launchpad X controller plan** and `mu-control` — read before any MIDI-input mapping or controller-output work. |
| [docs/design-naming.md](docs/design-naming.md) | **Naming conventions** — Layer vocabulary, namespaces, folders, identifiers. Read before naming a class, file, folder or parameter. |
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
- **mu-core never depends on a plugin** — strictly one-way: plugins link mu-core, never the reverse. The planned `mu-control` library (controller drivers) follows the same rule and `mu-core` must never include it. A `mu-core/**` file must not `#include` a plugin header or name a plugin symbol (`mu_clid::`, `mu_tant::`, …); mu-core knows only `ProcessorBase` + the shared interfaces. Enforced by `tests/scripts/check-core-boundary.ps1`. **When adding a file, decide its side first:** generic with no plugin-specific param IDs / semantics → `mu-core`; references product-specific concepts → that product's source tree. New plugin-specific code goes under the product's namespace (`mu_clid::`, `mu_tant::`) so the side is visible at every reference.
- **mu-core stays plugin-agnostic** — no `#include` of any product header from mu-core, no product-specific symbol names. Naming uses neutral terms (`channel` / `slot` / `layer` over `rhythm`). Use `MuLookAndFeel` directly from mu-core code, never the `MuClidLookAndFeel` back-compat alias.
- **Modulation depth = % of the target knob's range** — every product defines its modulation targets in one table of `mu_mod::ModTarget` rows (id, label, section, parameter); a new target is one appended row, never a hand-set scale. Rules + per-product tables in [docs/design-plugin-family.md](docs/design-plugin-family.md#modulation-targets--family-standard); enforced by `mu_mod::checks` in each product's tests and `tests/scripts/check-mod-targets.ps1`.
- **ModulationMatrix is the single reader** — audio engine reads only from `ModulationMatrix`, never directly from APVTS or `ControlSequence`.
- **Channels are fully self-contained** — `ControlSequence`s may only target parameters within their own channel. No cross-channel modulation. Global FX parameters are not valid modulation destinations.
- **ControlSequence lengths are independent** — never couple loop lengths or rates to channel step counts.
- **ModulationMatrix processes in dependency order** — detects and rejects circular dependencies at assignment creation time.
- **FXSlotBase interface for all FX** — enables VST3 plugin hosting in v3 without refactoring.
- **All colours and sizes in MuLookAndFeel only** — no hardcoded values in component drawing code.
- **All UI uses the shared component library** — never build a one-off version of a standard control. The editor shell ([mu-core/UI/EditorShellBase.h](mu-core/UI/EditorShellBase.h)) supplies LookAndFeel, TransportBar, StatusBar, About / Save / Preset Browser / MIDI preset overlays, overlay state machine, layout, keybindings — products supply only the sidebar + main panel + optional mixer / settings overlays. `KnobWithLabel`: bind data with `onValueChanged` or an attachment, never by overriding `getSlider().onValueChange` ([docs/design-ui-family.md](docs/design-ui-family.md#knobwithlabel-callbacks)).
- **Family consistency rule** — all project structuring (Source/ folder layout, docs/ topology, file extensions, naming conventions, ProcessorBase virtual hooks, backlog handling) must mirror across the mu-family (mu-clid / mu-tant / mu-toni / future siblings). Before adding a folder, naming a file, picking a convention for a new plugin, **mirror what the existing product does**. Before introducing a new convention for one product, **propagate it to the others**. Concrete current conventions: per-layer preset = camelCase noun (`.muRhythm`, `.muPattern`; layer types are `Rhythm` / `Pattern` / `Arp` / `Track` for mu-Clid / mu-Tant / mu-Toni / mu-On); full preset = plugin-name camelCase (`.muClid`, `.muTant`); Source subfolders = **the same eight in every instrument product, empty ones kept with a `.gitkeep`**: `{Audio, License, Modulation, Persistence, Plugin, Sequencer, Tests, UI}` (see [docs/design-naming.md](docs/design-naming.md)); product docs at `docs/<product>/` mirroring mu-clid's set as topics get decided.

## Code style (mandatory)

- **No backlog issue numbers in comments.** Enforced by `tests/scripts/check-comment-refs.ps1` (run as step 0 of `/build`; `ref-ok` in the comment marks a legitimate `#N`). Writing `// #123`, `// fix for #123`, `// added in #xxx`, or any other backlog reference in source code is forbidden. Backlog context belongs in commit messages and PR descriptions. Comments rot out of sync with the backlog and a stale `#NNN` reference is worse than no reference.
- **Comments must help Andy read and understand the code.** Concise, clear, and focused on the *why* and *what* (not the *how*, which the code itself shows).
  - **Loops** — comment the purpose of the loop. What is it doing as a whole? (`// Apply per-voice modulation across all active rhythms.`)
  - **Algorithms** — comment what the algorithm does, and cite the source if it's not obvious (`// ADAA tanh — Reiss & Stefanidis 2016`, `// Signalsmith FDN reverb`, `// Karplus-Strong delay-line feedback loop`). A reader should be able to look up the reference if they want to dig deeper.
  - **Section headers** — when a function contains clearly separate phases, put a one-line comment at the top of each phase naming its purpose and result (`// Phase 1: gather analysis frames → spectrum[]`).
- One sentence per comment is almost always enough. If you need more, the code probably wants to be split into named helpers instead.

## UI values

Knob colour coding, window sizing, and all layout constants are defined in [mu-core/UI/Components/MuLookAndFeel.h](mu-core/UI/Components/MuLookAndFeel.h). **Every shadow, highlight and tint strength lives in `MuTheme::Lighting`** ([mu-core/UI/Components/MuTheme.h](mu-core/UI/Components/MuTheme.h)) — with master `shadowAmount` / `highlightAmount` that scale them all for every product. Never hard-code an alpha for depth in drawing code; add a field there. **The metal style is the family-standard look** — every app turns it on with one `setMetalStyle(true[, appAccent])` call at the end of its editor constructor; the elements (metal panels, raised boxes, name plates, LCD selectors, lamps, engraved labels) and their rules are in [docs/design-ui-family.md §11](docs/design-ui-family.md#11-metal-style-family-standard). Build new UI from those mu-core helpers, never one-off drawing. Family-wide design notes in [docs/design-ui-family.md](docs/design-ui-family.md); product-specific layouts in `docs/<product>/design-ui.md`.

