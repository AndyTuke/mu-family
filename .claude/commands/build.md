Build the mu-family. Default to **Debug** (a normal code change). Build **Release** only if $ARGUMENTS contains "release" or "full" — never build Release on your own initiative.

## Sequence

0. **Source checks first:** `pwsh tests/scripts/check-comment-refs.ps1 -Quiet` (no backlog numbers in source comments). A FAIL is fixed before building — reword the comment, keep the reason, drop the number.
1. **Reconfigure first, always:** `cmake -B build` (fast ~2s; regenerates icon.ico, versioninfo.rc, and the build-number RC bake — skipping it ships stale artefacts).
2. **Focused Debug** (normal work on one or two apps — owner rule 2026-10-12): `cmake -B build -DMUFAMILY_FOCUSED_BUILD=ON`, then build only those apps' targets (e.g. `cmake --build build --config Debug --target mu-tant_Standalone mu-tant-tests`; add `mu-core-tests` when shared code changed). The number is **not** incremented. Build and test only when really required.
2b. **Stage-boundary Debug** (a ruled stage closing, or when Andy says so; all products): `cmake -B build -DMUFAMILY_FOCUSED_BUILD=OFF`, then `cmake --build build --config Debug`. This increments the number by exactly 1; build every test target explicitly too (`mu-clid-tests` and the others are not in ALL).
3. **Full build** (only if asked — "full" or "release"): `cmake --build build --config Debug && cmake --build build --config Release`
   - Release builds stay local; nothing is deployed.

## Build-number policy (owner rules — do not work around)

- **Every full (stage-boundary) Debug build increments the number by exactly 1.** No time/session throttle. A focused build (`-DMUFAMILY_FOCUSED_BUILD=ON`) reuses the current number and never increments.
- **Release never increments** — it reuses the last Debug number and rebuilds only Release artefacts (Debug untouched).
- **Release ≤ last Debug, always.** A Release coming out *higher* than the last Debug means `IncrementBuildNumber.cmake` aborts with FATAL_ERROR — surface it to the owner, don't bypass.
- **Never bump `build_number.txt` manually.**

## If the exe shows a stale version (mu-tant especially)

`cmake --build --target mu-tant` (the aggregate) rebuilds SharedCode.lib but may not relink the exes. Build the explicit format targets instead:
`mu-tant_Standalone mu-tant_VST3 mu-tant_CLAP` (add `mu-tant-tests`). Windows file-properties version only refreshes on reconfigure — trust `BuildNumber.h` / the About panel (`v1.1.0.N`) as source of truth.

## Reporting

- Read the number from `mu-core/BuildNumber.h` **after the final build** — never quote an intermediate build log (goes stale on rebuild).
- Report "Debug clean at v1.1.NNN" or "Debug + Release clean at v1.1.NNN".
- End the response with the `## Debug builds` / `## Release builds` artefact list (the `mu_build_artifacts.ps1` hook injects it as additionalContext). Only list configs that exist on disk.

## After the build

- Hand any compiler warnings the warning-scan hook reported to the `backlog-administrator` agent to log; then ask it for the next task and keep working until the next build is worth it (CLAUDE.md, "Keep working until a build is worth it").
- If the work since the last `/notes` fixed/improved anything **user-facing**, run `/notes` once for the batch to add the In-Testing one-liners and push the site.
