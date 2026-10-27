Ship a complete release of the mu-family. **Only run when the owner explicitly asks for a release.** Run the whole pipeline beginning to end — do not stop after the local build. A Release reuses the last Debug number (never increments).

## 1. Build (local Windows)

1. Reconfigure: `cmake -B build` (Release builds stay local; nothing is deployed).
2. Build Debug (bumps the number), then Release (reuses it):
   `cmake --build build --config Debug && cmake --build build --config Release`
   - Run builds through the **Bash tool** so the artifact/warning/deploy hooks fire. Heavy build → run in background.
   - Release ≤ last Debug always; a higher Release aborts with FATAL_ERROR — surface it, don't work around.
   - If exe versions look stale, build the explicit format targets (see `/build`).
3. Read the final number from `mu-core/BuildNumber.h`.

## 2. Pin the build number for CI

CI checks out the pushed commit and reads `build_number.txt`. Commit + push the bump:
`git add build_number.txt mu-core/BuildNumber.h && git commit && git push` (message: `Release v1.1.NNN — build-number bump…`, with `Version: v1.1.NNN`).

## 3. (b) GitHub release — all platforms via CI

**Windows only** (owner, 2026-10-27): Linux is no longer built or shipped. Dispatch the workflow (it builds Windows, packages the fixed-name zips the site links to, and creates/updates the GitHub Release `v1.1.0.NNN` from `build_number.txt`):

`gh workflow run release.yml --ref main`

Then confirm it started: `gh run list --workflow=release.yml --limit 1`. (Do **not** hand-build zips or carry forward stale platform binaries — CI builds both fresh. **macOS is on hold** — no Apple signing certificate, so no Mac build ships; don't re-add it or dispatch `mac-validate.yml` unless the owner lifts the hold.) `release.yml` is the only workflow dispatched as part of a release; `ci.yml` + `mac-validate.yml` still run only on explicit owner request, and none run on push.

## 4. (c) Promote release notes + bump download page

For each affected product (`site/mu-clid-releases.html`, `site/mu-tant-releases.html`):
- Turn the "Next release · In testing" section into a dated `v1.1.0.NNN` "Current" release (`rel-pill testing` → `rel-pill current`, `Next release`/`in testing` → `v1.1.0.NNN`/`released <D Month YYYY>`).
- Insert a fresh empty In-Testing section above it; demote the previous release (remove its `rel-pill current`).
- `site/download.html`: bump the hardcoded `data-version`/`data-reldate` fallbacks (live values come from the GitHub latest-release tag via JS, so this is just the pre-JS default).
- **Update-check data:** run `pwsh scripts/write-latest-json.ps1 -Products <slugs released this time>` (slugs: mu-clid, mu-clid-midi, mu-tant, mu-toni, mu-on, mu-link; a product left out keeps its old entry), then `pwsh tests/scripts/check-latest-json.ps1`. This writes `site/latest.json`, which every app reads to show its "Update available" link. Push it only once the CI run has finished and the GitHub release assets exist, so the link never points at a download that is not there yet.
- Commit + push the site (Netlify auto-deploys; no CI).

## 5. Report

Report the build number (from `BuildNumber.h`), the dispatched CI run URL, and the site push. End with the `## Release builds` artefact list. Note that the GitHub release assets appear once CI finishes.
