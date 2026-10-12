# Workflow efficiency — faster build/dev rounds

Decision record (owner, 2026-10-12). Two machines: the **work laptop** (cannot build; design, docs, review, backlog) and the **build PC** (builds, runs tests, hosts the main Claude thread and its agents).

## Decision

A permanent multi-agent structure (one dev agent per product plus a core dev, with a coordinator running a build barrier) was considered and **rejected for now**. The build is serial — every Debug build compiles all products and bumps one family-wide number — so parallel coders mostly add unbuilt code per round, bigger failed-build batches, and coordination and cold-start cost. Parallel worktree agents remain available **ad hoc** for a round of independent, product-specific items.

Instead, shorten the round itself: **task → code → build → test → fix**.

## The five actions

1. **Measure first.** Time one typical round and record where the minutes go (planning, coding, configure, compile, link, unit tests, listening tests, review, fixes). Fix the largest piece first. Record the numbers here.
2. **Faster builds.** Incremental builds, a faster linker, compiler caching (ccache or equivalent), and building only the products that changed. The "all products on every Debug build" rule is the main cost driver; any relaxation needs an owner decision because it touches the build-number invariants in the root CLAUDE.md.
3. **Cheaper verification.** Make the unit tests and the listening-test render fast enough to run on every build, ending in a short pass/fail summary the owner can read at a glance.
4. **Batch and queue the work.** `backlog-administrator` prepares the next 3–5 task briefs ahead of time so a build session starts without planning time. A `check-all.ps1` runs the static gates (`check-backlog`, `check-core-boundary`, `check-mod-targets`, `check-comment-refs`) before the build, so a build never fails on something a script could have caught.
5. **Use the work laptop ahead of the build PC.** The laptop does design, docs, manuals, release notes, backlog grooming and read-only review (`dsp-controller`, `/code-review`); `test-steward` may write tests there but they stay unrun until the build PC builds. Anything changed there but never compiled is listed under "verify on build PC" so the build PC does one Debug build and a single verification pass.

## Status

| # | Action | Status |
|---|---|---|
| 1 | Measure a round | Not started |
| 2 | Faster builds | Not started |
| 3 | Cheaper verification | Not started |
| 4 | Batch/queue + `check-all.ps1` | Not started |
| 5 | Laptop-ahead workflow + "verify on build PC" list | Not started |

Backlog entries for these are created by `backlog-administrator`, not here.
