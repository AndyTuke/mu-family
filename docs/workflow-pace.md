# Working pace — keep it fast

Owner rules, 2026-10-21 (Andy): the day-to-day workflow had become slow and laborious compared with how the first products were built. These rules **override the heavier parts of the round procedure** in the root [CLAUDE.md](../CLAUDE.md) (reviewer calls, test cadence, build cadence). The longer-term plan for the build itself is in [design-workflow-efficiency.md](design-workflow-efficiency.md).

## The question to ask first

Before any check, test, build, review or agent call, ask: **"Is this really required? Is it going to be a benefit, or shall we skip it, knowing that rolling back or fixing forward will still be quicker?"** If rolling back or fixing forward is quicker than the check, skip the check.

## Rules

1. **Tests.**
   - During the day run only what is needed to decide the next step: usually compile what was touched and run the suite of the one product it touches.
   - **One full test sweep at the end of each day** (or when the owner says the day is done) covers all of the day's work. It runs in the background and reports which test failed and the commit range for the day, so a failure is quick to trace.
   - The slow golden suites (mu-Tant, mu-On, mu-tronic) run only in the daily sweep and before a release. Keep commits small and separate so a late failure is easy to attribute.
   - **Goldens are a gate for behaviour-preserving moves, not for every build** (owner, 2026-10-31). Record them at a stage boundary (or on the commit just before a refactor); while sounds are changing they stay stale and their failures are not chased. They run only when the audio path changed, in Release or with a long cap (the Debug mu-Tant and mu-Toni runs exceed 5 minutes). The saved-state hash (`*.state.hash`) is information only: the comparison ignores it, the audio, peak and MIDI lines are the gate.
2. **Reviews.**
   - The `dsp-controller` is **not** called routinely. Read audio-thread changes carefully yourself; call it only when the owner asks or the change is genuinely risky (new audio-thread locking or allocation, a new lock-free structure).
   - Other reviewers (`ux-controller`, `architecture-steward`) are called when a decision is truly new, not for every step of a feature. One combined pass per feature, no re-review rounds for minor findings.
3. **Builds.**
   - Focused builds while working. One full Debug build of all products when the owner needs something to try or at a real stage boundary. Release only when the owner says.
   - Build once after a merge, not once per agent step.
4. **Agents and worktrees.**
   - Edit directly in the main checkout for small items and use the existing incremental build. Use an isolated worktree only for a large piece of work running in parallel.
   - A worktree is a full copy with its own build (5 to 16 GB). Remove it as soon as its branch is merged.
5. **Backlog and docs.** Batch the backlog update at the end of a round (one `backlog-administrator` call), not after each step. Docs and manuals follow the work in a batch, not per step.
6. **Scope.** No large new feature while the Open list is long, unless the owner asks for it.
7. **Messages.** Short. No recaps of what the owner can already see. Ask the owner only what genuinely needs them.

## Disk

- **Never build or put large files on C:** (the system disk). Builds, worktrees and test output go on D: only. If D: runs low, delete merged worktrees and stale build output; never fall back to C:.
- Keep `D:\Dev\mu\build` to the latest Debug and Release builds: no old installers, no leftovers from renamed products.
- Scratch under the session temp folder (on C:) is kept small; delete old screenshot and test-copy folders when done.
