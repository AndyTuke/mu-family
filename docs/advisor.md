# Advisor context

Facts about this repo for the scheduled advisor routines. Each fact names the file it came from. Facts the repo states are recorded as found; nothing here is a suggestion.

## Stack

**Framework and language**
- JUCE 9.0.4, pinned in `juce-version.txt`. CMake aborts if the checkout at `JUCE_PATH` differs from the pin, and every CI workflow checks out the same tag (`CMakeLists.txt`, `.github/workflows/release.yml`). JUCE is not vendored; locally it is `D:\JUCE` via the `JUCE_PATH` environment variable (`CLAUDE.md`).
- C++17 (`mu-core/CMakeLists.txt`: `cxx_std_17`).
- Owner rule: keep JUCE current and use current JUCE APIs, never deprecated ones (`CLAUDE.md`).

**Products and plugin formats**
- Plugins built on the shared `mu-core` INTERFACE library (`CMakeLists.txt`, `CLAUDE.md`):
  - mu-Clid: VST3, CLAP, Standalone.
  - mu-Tant: VST3, CLAP, Standalone.
  - mu-Toni: VST3, CLAP, Standalone.
  - mu-On: VST3, CLAP, Standalone.
  - mu-Clid MIDI: VST3, CLAP, no Standalone, no audio (`mu-clid-midi/CMakeLists.txt`).
- Desktop app, not a plugin: mu-link (local audio receiver, master clock and performance mixer; GUI app target in `mu-link/CMakeLists.txt`).
- Controller library: `mu-control` (Launchpad X driver, mapping, surface model; INTERFACE library that depends on `mu-core`, never the reverse) (`CMakeLists.txt`).
- Dev tools, not shipped: `ControlDesign` (UI sandbox), `tools/license-tool` (licence key generator and signer) (`CMakeLists.txt`).
- AU exists only on Apple through `MUFAMILY_AU_FORMAT` (`CMakeLists.txt`). macOS is on hold: no Apple signing certificate, so nothing ships for Mac (`CLAUDE.md`, `.github/workflows/release.yml` header).
- Clap support comes from `ThirdParty/clap-juce-extensions` (git submodule, `.gitmodules`).

**Build system and toolchain**
- CMake, minimum 3.22 (`CMakeLists.txt`). Local CMake is 4.3.1 (bundled with Visual Studio), generator "Visual Studio 18 2026", MSVC 19.51 (`build/CMakeCache.txt`, `build/CMakeFiles/*/CMakeCXXCompiler.cmake`).
- Python 3.12.10 for the listening-test pipeline (`tests/scripts/analyse.py`, `tests/scripts/run-listening-tests.py`).
- Shared build number in `build_number.txt` (currently 1411), versions in `version.txt` and `version-pre.txt` (both 1.1), `mu-core/BuildNumber.h` is generated (`CLAUDE.md`, `cmake/IncrementBuildNumber.cmake`).
- Windows installer built with Inno Setup (`cmake/BuildInstaller.cmake`, `mu-clid/installer/mu-Clid.iss`).
- Static checks run as step 0 of the build skill: `tests/scripts/check-*.ps1` (backlog format, comment references, core boundary, JUCE version, mod targets, build artefacts).
- Windows runtime is embedded (`CMAKE_MSVC_RUNTIME_LIBRARY` MultiThreaded, `CMakeLists.txt`).

**Target platforms**
- Windows: the shipped, primary target (`CLAUDE.md`).
- Linux: built by CI and shipped as Beta zips; ubuntu-latest, with libx11, freetype, fontconfig, GL and related dev packages installed in the workflow (`.github/workflows/release.yml`).
- macOS: on hold (see above).

**CI**
- Four workflows in `.github/workflows/`, all `workflow_dispatch` only; none runs on push or pull request (`CLAUDE.md`, each workflow's `on:` block):
  - `release.yml`: builds Release plugins on ubuntu-latest and windows-latest in a matrix, packages fixed-name zips, then a publish job creates or updates the GitHub Release tagged `v<version>.0.<build>`. Windows-only mu-link build step. `--parallel` build; no cache yet.
  - `ci.yml`: cross-platform build and unit-test safety net, run on request.
  - `mac-validate.yml`: macOS universal build, pluginval and auval, run on request (on hold).
  - `sync-backlog.yml`: syncs GitHub issues into the backlog.
- Actions used: `actions/checkout@v4`, `actions/upload-artifact@v4`, `actions/download-artifact@v4` (`release.yml`).
- Local plugin validation: Tracktion pluginval over each VST3 (`tests/scripts/run-pluginval.ps1`). CLAP and Standalone are not covered by pluginval.

**Third-party libraries (vendored under `ThirdParty/`)**
- `clap-juce-extensions`: submodule at commit `e8de9e8`, 107 commits after tag 0.26.0 (`git describe`).
- `signalsmith-basics` and `signalsmith-dsp`: vendored sources; no version recorded in the repo (`ThirdParty/*/CMakeLists.txt` only set the CMake minimum 3.24).
- `monocypher`: vendored, Ed25519 licence verification; header says "version __git__", no version recorded (`ThirdParty/monocypher/monocypher.h`).
- Online licensing talks to Lemon Squeezy through `mu-core/License/LemonSqueezyClient.cpp`.

**Website and services**
- Static HTML site in `site/`, hosted on Netlify. Netlify builds only when `site/` or `netlify.toml` changes (`netlify.toml`). Local preview: `tests/scripts/serve-site.ps1`.
- Site work happens on the local branch `site-local` in a separate worktree (`D:\Dev\mu-site`) and is merged to `main` in one batch (`.claude/agents/web-designer.md`).
- Sales and downloads: Lemon Squeezy pay-what-you-want checkout; zips from GitHub Releases (`site/download.html`, each product's `Source/License/LicenseKey.h`). Mailing list: MailerLite embedded form (`site/download.html`).

## Claude Code setup

**Exists**
- `CLAUDE.md` at the repo root plus one per product: `mu-clid/`, `mu-clid-midi/`, `mu-tant/`, `mu-toni/`, `mu-on/`, `mu-link/` (about 600 lines in total).
- `.claude/settings.json` (checked in): allows Bash, PowerShell, WebFetch and WebSearch without prompts; PostToolUse hooks on Bash that run `post_build_todo.ps1`, copy Release artefacts to the OneDrive Windows folder, scan build output for compiler warnings (an agent hook), run the first-build-of-day oversight trigger (`scripts/mu_build_hook.ps1`) and collect build artefacts (`scripts/mu_build_artifacts.ps1`); a UserPromptSubmit hook that injects pending oversight audit text (`scripts/mu_oversight_inject.ps1`).
- `.claude/settings.local.json` (per machine): extra command allowances and a SessionStart hook running `.claude/hooks/comms-start.ps1` (untracked).
- Subagents in `.claude/agents/`: `backlog-administrator`, `architecture-steward`, `dsp-controller`, `ux-controller`, `test-steward`, `user-documentation-author`, `web-designer`. Each owns one area and writes only its own files; `CLAUDE.md` has the precedence table.
- Slash commands in `.claude/commands/`: `bug`, `build`, `commit`, `deepaudit`, `notes`, `optimise`, `release`, `update`.
- Hooks scripts in `.claude/hooks/`: `post_build_todo.ps1`, `comms-start.ps1`.
- User-level memory (`C:\Users\andyt\.claude\projects\d--Dev-mu\memory\`), not in the repo: owner preferences and project state.

**Does not exist**
- No `.mcp.json` and no project MCP servers in the repo. The interactive session has user-level connectors (Microsoft 365, Claude Docs, Discord) that are not configured here.
- No `.claude/skills/` folder (skills in use come from the harness and the commands folder).
- No GitHub Actions workflow that runs on push, by owner rule.
- No Git pre-commit hooks (`.git/hooks` has only samples).

## Decisions

Decisions the owner has made that the advisor must respect and not re-suggest. Add one line per decision with its date. Answers below come from the owner; the repo-derived ones are marked as such.

- macOS and AU are on hold: no signing certificate, so no Mac builds ship and `macos-latest` stays out of the release matrix (repo: `CLAUDE.md`, `release.yml`). Re-enabling needs the owner's word.
- JUCE is pinned and kept current through the single pin in `juce-version.txt`; bump only through the documented procedure (repo: `CLAUDE.md`).
- No workflow runs on push or pull request, ever (repo: `CLAUDE.md`).
- Release builds only when the owner asks; Debug is the working build (repo: `CLAUDE.md`).
- The backlog (`backlog.md`) is written only by the `backlog-administrator` agent (repo: `CLAUDE.md`).
- The owner edits `CLAUDE.md` files himself; the advisor must not propose edits to them as ready-to-apply changes (stated in session, 2026-10-14).
- The website stays on its local `site-local` branch until a batched publish; the four paid apps are Pay What You Want, mu-Clid MIDI and mu-link are freeware (repo and session, 2026-10-15).

Answers from the owner (2026-10-15):
- **Scope:** workflow and tooling only (Claude Code setup, agents, hooks, CI, build and test process). No product, DSP, UI, pricing or website feature ideas.
- **Config edits:** suggest only, never edit. Report the exact change; the owner or the main conversation applies it. The advisor never edits `CLAUDE.md`, settings, hooks, agents or commands.
- **Cost:** free tier only. Nothing that adds cost; anything that would cost money is flagged for the owner's decision, not recommended.
- **Platforms:** Windows only. Ignore Linux and macOS workflow improvements.
- **Volume:** at most 3 suggestions per run, highest value first; lower-value ones wait for a later run.

## Reported

The advisor routines append a dated entry here for every suggestion they make, so the same thing is never reported twice. Format: `YYYY-MM-DD: short title (file or area): what was suggested, and the outcome if known.`

