Build the mu-family. Default to **Debug** (a normal code change). Build **Release** only if $ARGUMENTS contains "release" or "full" — never build Release on your own initiative.

## Sequence

1. **Reconfigure first, always:** `cmake -B build` (fast ~2s; regenerates icon.ico, versioninfo.rc, and the build-number RC bake — skipping it ships stale artefacts).
2. **Debug** (any code change, all products): `cmake --build build --config Debug`
3. **Full build** (only if asked — "full" or "release"): `cmake --build build --config Debug && cmake --build build --config Release`
   - Release deploys to OneDrive **only** when configured with `-DMUFAMILY_DEPLOY_TESTERS=ON`. Without the flag it stays local. Debug never deploys.

## Build-number policy (owner rules — do not work around)

- **Every Debug build increments the number by exactly 1.** No time/session throttle.
- **Release never increments** — it reuses the last Debug number and rebuilds only Release artefacts (Debug untouched).
- **Release ≤ last Debug, always.** A Release coming out *higher* than the last Debug means `IncrementBuildNumber.cmake` aborts with FATAL_ERROR — surface it to the owner, don't bypass.
- **Never bump `build_number.txt` manually.**

## If the exe shows a stale version (mu-tant especially)

`cmake --build --target mu-tant` (the aggregate) rebuilds SharedCode.lib but may not relink the exes. Build the explicit format targets instead:
`mu-tant_Standalone mu-tant_VST3 mu-tant_CLAP` (add `mu-tant-tests`). Windows file-properties version only refreshes on reconfigure — trust `BuildNumber.h` / the About panel (`v1.0.0.N`) as source of truth.

## Reporting

- Read the number from `mu-core/BuildNumber.h` **after the final build** — never quote an intermediate build log (goes stale on rebuild).
- Report "Debug clean at v1.0.NNN" or "Debug + Release clean at v1.0.NNN".
- End the response with the `## Debug builds` / `## Release builds` artefact list (the `mu_build_artifacts.ps1` hook injects it as additionalContext). Only list configs that exist on disk.

## After the build

- Fix any compiler warnings the warning-scan hook logged to backlog.
- If the build fixed/improved anything **user-facing**, run `/notes` to add the In-Testing one-liner and push the site.
