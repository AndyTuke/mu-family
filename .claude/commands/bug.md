Delegate to the `backlog-administrator` agent (Agent tool, subagent_type `backlog-administrator`) to add a new Open issue for the following bug report: $ARGUMENTS

Do not edit `backlog.md` yourself; the agent is the only thing that writes to it. Give it the bug report verbatim plus any file:line references or context from this conversation, and tell it this is a bug (`[area/topic]` title, finding, fix, and what is unverified). It applies the numbering, placement (top of Open), row-format and one-issue-per-row rules, runs `check-backlog.ps1`, and returns the issue number(s) it created.

Do not ask for confirmation. Relay the new issue number(s) to the user.
