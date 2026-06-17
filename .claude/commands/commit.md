Stage and commit the current changes with a properly-formatted mu-family commit message, then push. Use $ARGUMENTS as the subject/summary hint if given; otherwise derive it from the diff.

## Gather

1. `git status` + `git diff` (and `git diff --staged`) to see what changed.
2. Read `build_number.txt` for the current build number → version is `v1.0.<build>`.
3. Identify which backlog items this work closes — read `backlog.md` and match the changes to issue numbers/descriptions.

## Message format (mandatory — see CLAUDE.md)

Every commit message includes:
1. **Stage(s)** in the subject when the work maps to a dev stage (e.g. `Stage 13: …`). Non-staged tooling/docs work can use a plain descriptive subject.
2. **Issues closed** — one `Closes #NNN: <one-line description>` line per backlog item the commit resolves.
3. **Full version** — a `Version: v1.0.<build>` line from `build_number.txt`.

End the message with:
```
Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
```

Example:
```
Stage 13: UI completions — Amp FX sends, intra-FX wiring verified

Closes #17: Amp FX send knobs added to Voice Amp row
Closes #22: Intra-FX APVTS wiring verified end-to-end

Version: v1.0.925

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
```

## Commit & push

- This repo commits to `main` directly (repo convention). Stage the relevant files, commit with the message above, then `git push`.
- Use a single-quoted heredoc for the multi-line message so `$` and backticks aren't expanded.
- **Never** use `--no-verify` or skip signing unless the owner explicitly asks. If a hook fails, fix the cause.
- A site-only change auto-deploys via Netlify; no CI runs on push (workflows are dispatch-only — never trigger them as a side-effect).

## After

Update `backlog.md` so any items closed by this commit move to the **Fixed** group (descending order preserved).
