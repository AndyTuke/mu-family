Stage and commit the current changes with a properly-formatted mu-family commit message, then push. Use $ARGUMENTS as the subject/summary hint if given; otherwise derive it from the diff.

## Gather

1. `git status` + `git diff` (and `git diff --staged`) to see what changed.
2. Read `build_number.txt` for the current build number → version is `v1.1.<build>`.
3. Identify which backlog items this work touches — ask the `backlog-administrator` agent (it reads `backlog.md`; you do not edit it) to match the changes to issue numbers/descriptions and to say which have every applicable reviewer sign-off.

## Message format (mandatory — see CLAUDE.md)

Every commit message includes:
1. **Stage(s)** in the subject when the work maps to a dev stage (e.g. `Stage 13: …`). Non-staged tooling/docs work can use a plain descriptive subject.
2. **Issues** — one `Closes #NNN: <one-line description>` line per backlog item the commit resolves **and that has every applicable reviewer sign-off**; use `Refs #NNN: <what this commit did>` for an item that is still waiting on a reviewer or a build check. Docs and tooling commits with no item say `Issues closed: none`.
3. **Full version** — a `Version: v1.1.<build>` line from `build_number.txt`.

End the message with the attribution line the session provides (the model name changes between sessions, so do not hard-code one).

Example:
```
Stage 13: UI completions — Amp FX sends, intra-FX wiring verified

Closes #17: Amp FX send knobs added to Voice Amp row
Closes #22: Intra-FX APVTS wiring verified end-to-end

Version: v1.1.972

<attribution line from the session>
```

## Commit & push

- This repo commits to `main` directly (repo convention). Stage the relevant files, commit with the message above, then `git push`.
- Use a single-quoted heredoc for the multi-line message so `$` and backticks aren't expanded.
- **Never** use `--no-verify` or skip signing unless the owner explicitly asks. If a hook fails, fix the cause.
- A site-only change auto-deploys via Netlify; no CI runs on push (workflows are dispatch-only — never trigger them as a side-effect).

## After

Hand the `backlog-administrator` agent the issues this commit closes (and the `Refs` items, so it updates their Updates field) and the build number read from `mu-core/BuildNumber.h`, so it moves them to the **✅ Closed** group (descending order preserved) and runs `check-backlog.ps1`. Do not edit `backlog.md` yourself. If the agent changes the file, include that change in a follow-up commit and push.
