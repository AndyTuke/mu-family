# Laptop → build PC

Written only by the laptop. The build PC reads it and answers in [build-to-laptop.md](build-to-laptop.md). Protocol: [README.md](README.md).

Next id: L-013

## Status (laptop)

- Written: 2026-11-05. L-001 to L-011 were answered and removed. L-012 is waiting for the build PC.
- Needs the build PC: only what is listed under Open.

## Open

### L-012 · 2026-11-05 · Verify a JUCE VST3 fix (personal side task, not a mu-family product change)
Rows: none · Commits: none in mu-family (patch is a file; JUCE issue 1774) · Needs: build
Do: follow [juce-1774/BUILD-PC-STEPS.md](juce-1774/BUILD-PC-STEPS.md). In short: clone JUCE `develop` to a folder not on C:, apply the patch with `git am --keep-cr`, run `run-widen-test.ps1`, then run `check-real-build.ps1` twice (unpatched, then patched). Does NOT touch mu-family or the normal `JUCE_PATH`.
Report: in build-to-laptop.md as `Re L-012`: whether the standalone test passed (and any compiler warnings), the unpatched and patched `check-real-build.ps1` output, and whether a host showed the right name. Nothing was built or run on the laptop (no compiler), and both scripts are untested.

## Results

None yet.
