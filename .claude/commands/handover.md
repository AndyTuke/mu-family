Hand work between the work laptop and the build PC. `/handover start` at the beginning of a session, `/handover end` at the end; with no argument, do `start`. Protocol and formats: [handover/README.md](../../handover/README.md). Use $ARGUMENTS to choose `start` or `end`.

## Which PC am I?

1. `git config --local mufamily.role` (`laptop` or `build`) wins.
2. Otherwise `build` when `JUCE_PATH` is set and the folder exists, else `laptop`.
3. Say which one you decided in one line. If it looks wrong, tell the owner to run `git config --local mufamily.role <laptop|build>`.

My outgoing file is `handover/laptop-to-build.md` (laptop) or `handover/build-to-laptop.md` (build PC); the other file is the one I read. **Never edit the other PC's file.**

## start

1. `git pull --rebase origin main` (this repo works on `main`; if the branch is not `main`, pull `origin main` explicitly and push with `git push origin HEAD:main`).
2. Read the other PC's file. Report to the owner, briefly:
   - its `## Status`;
   - every `## Open` item addressed to me (id, title, rows, what it needs);
   - the `## Results` entries answering MY items (`Re <my id>`), with Done / Failed / Partly.
3. For each result that answers one of my items: act on it (a build PC result with a build number lets the `backlog-administrator` close or update the row; a failure becomes work), then delete the item from MY file's `## Open`, and tell the owner.
4. Prune my own `## Results`: delete every `Re <id>` whose id is no longer under `## Open` in the other PC's file (the sender removed it, so it is acknowledged).
5. Commit only `handover/` (subject `Handover: <laptop|build PC> read <date>`), push, and then start the owner's work. Skip the commit when nothing changed.

## end

1. List what this session did that the other PC must still do or see: unbuilt or unrun source, tests written but not run, screenshots or listening needed, decisions that need the other machine, or a failure found.
2. In MY file: add one `## Open` item per piece (format in the README, next id from `Next id`, then bump it). Point at the backlog row and commit hashes; do not copy row text. Say exactly what is unbuilt, unrun or unseen. Update `## Status`: date, `git rev-parse --short HEAD`, whether everything is pushed, and the build number from `mu-core/BuildNumber.h` on the build PC.
3. If I answered items from the other PC this session, put the `### Re <id>` entries under MY `## Results` (build PC: build number, what was run, what was seen; never record a pass that was not observed).
4. Make sure real work is committed and pushed first (use `/commit`), then commit only `handover/` (subject `Handover: <laptop|build PC> end of session <date>`) and `git push` (`git push origin HEAD:main` when not on `main`). Tell the owner the handover is pushed and list the open items for the other PC in one line each.

## Rules

- The backlog is the record of the work; a handover item holds only the concrete steps and results for the other machine. Backlog changes still go through the `backlog-administrator`.
- Keep entries short and concrete: commands to run, what to look at, what to bring back.
- If `git pull` conflicts inside `handover/`, someone edited the wrong file: keep the owner's version of each PC's own file, tell the owner, and do not guess.
