Ship a Release of the mu-family. **Only run when the owner explicitly asks for a release.** A Release reuses the last Debug number (never increments) and rebuilds only Release artefacts.

## Pre-flight

1. Confirm a Debug build exists at the intended number (`mu-core/BuildNumber.h`). Release must be ≤ last Debug.
2. Reconfigure with tester deploy on: `cmake -B build -DMUFAMILY_DEPLOY_TESTERS=ON`
   (`MUFAMILY_DEPLOY_TESTERS` is OFF by default; ON copies every product to the OneDrive tester share via POST_BUILD.)

## Build

`cmake --build build --config Release`

If the exe versions look stale, build the explicit format targets (see `/build`): the aggregate target may not relink the Standalone/VST3/CLAP exes.

## Ship — all three, every time (CLAUDE.md cardinal rule)

1. **OneDrive tester share** — handled by the `DEPLOY_TESTERS` POST_BUILD: mu-clid (+ Lite), mu-tant, mu-on, mu-toni, mu-link's exe. Verify the Windows artefacts landed in `TDP/Windows`.
2. **GitHub "latest release" zip** — build a zip and upload it so the website download links resolve.
3. **Promote release notes** — for each affected product (`site/mu-clid-releases.html`, `site/mu-tant-releases.html`):
   - Move the accumulated "Next release · In testing" items into a new dated `v1.0.NNN` section.
   - Clear the In-Testing section.
   - Bump the hardcoded version default in `site/download.html`.
   - Commit + push the site (Netlify auto-deploys; no CI).

## macOS / Linux (only if owner asks)

Windows builds locally. Mac/Linux come from `release.yml` CI — `gh workflow run release.yml` builds all products × platforms from the pushed commit; download and copy artefacts into `TDP/Mac` + `TDP/Linux`. **Never dispatch CI as a side-effect** — owner must explicitly request it.

## Reporting

Read the number from `mu-core/BuildNumber.h` after the build; report "Release clean at v1.0.NNN" and confirm all three ship steps done. End with the `## Release builds` artefact list.
