# Handover between the two PCs

How the work laptop and the build PC hand work to each other (owner request, 2026-10-30). The mechanism is three small files and a slash command. It carries only what the backlog cannot: the **concrete steps** one machine needs the other to do, and the **results** that come back.

| PC | Role | Writes |
|---|---|---|
| Laptop (review-only, cannot build) | design, review, source written unbuilt, backlog logging and ranking | [laptop-to-build.md](laptop-to-build.md) |
| Build PC (builds, tests, releases) | build, run, listen, screenshots, close rows with build evidence | [build-to-laptop.md](build-to-laptop.md) |

## Rules

1. **Each PC writes only its own file.** The other PC reads it and never edits it, so git never has two writers on one file and pulls do not conflict.
2. **The backlog stays the record of the work.** A handover item points at the backlog row (`#1703`) and the commit, and gives the steps for this machine. It never copies row text. The `backlog-administrator` still owns `backlog.md`; closing a row still needs the build evidence.
3. **An item is finished when it is gone from the sender's file.** The receiver answers in its own file under `## Results`, quoting the item id. The sender reads the result, acts on it (for example closes the row), then deletes the item. The receiver then deletes its own result, because an id that is no longer open in the other file means "acknowledged".
4. **Ids are never reused.** Laptop items are `L-001`, `L-002`, …; build PC items are `B-001`, …. Each file keeps its `Next id` line.
5. **Pull before reading, push after writing.** Start a session with `/handover start`, end it with `/handover end` ([../.claude/commands/handover.md](../.claude/commands/handover.md)). Commit only the `handover/` files in a handover commit.
6. **Say what you did not do.** Every item names what is unbuilt, unrun or unseen, and a result says exactly what was run and what was seen. Never record a pass that was not observed.

## Item format (in the sender's file, under `## Open`)

```text
### L-004 · 2026-10-31 · <short title>
Rows: #1703 · Commits: 0a335b69, b6df7df2 · Needs: build | listen | look | decide | review
Do: <the concrete steps for the other PC, one line each>
Report: <what to bring back: build number, test output, what you saw>
```

## Result format (in the receiver's file, under `## Results`)

```text
### Re L-004 · 2026-11-01 · Done | Failed | Partly
Build: v1.1.<number> (read from mu-core/BuildNumber.h) · Ran: <what was run> · Saw: <what was observed>
Notes: <failures with the first error line, anything the sender must fix or decide>
```

## Which PC am I?

`/handover` decides in this order: `git config --local mufamily.role` (`laptop` or `build`); otherwise `build` when `JUCE_PATH` is set and the folder exists; otherwise `laptop`. If that guess is wrong, set it once with `git config --local mufamily.role build`.

## What does not go here

Design decisions (design docs), work items (backlog), history (git log, `docs/DevelopmentHistory.md`), and anything that never needs the other machine. Gitignored things such as the JUCE checkout and the signing keys do not travel through git; if a task needs one, the item says which PC has it.
