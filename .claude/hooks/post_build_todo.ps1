# post_build_todo.ps1 — PostToolUse hook: after a cmake build, echo the backlog's Open
# section so the open issues are in front of us straight after the build, as CLAUDE.md's
# "after every build, fix open issues" rule expects.
#
# Reads the hook's JSON payload on stdin and does nothing unless the Bash command was a
# cmake build. Every failure is swallowed: this runs on every Bash call, so a broken hook
# must never interrupt the session.
#
# Windows PowerShell 5.1 compatible — the hook is invoked via `powershell`, like its
# siblings in .claude/settings.json.

try {
    $payload = $input | Out-String
    if ([string]::IsNullOrWhiteSpace($payload)) { return }

    $data = $payload | ConvertFrom-Json
    $cmd  = $data.tool_input.command
    if (-not $cmd -or $cmd -notmatch 'cmake --build') { return }

    $backlog = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'backlog.md'
    if (-not (Test-Path -LiteralPath $backlog)) { return }

    # The status markers are emoji, so read and write UTF-8 explicitly.
    try { [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false) } catch { }
    $lines = [System.IO.File]::ReadAllLines($backlog, [System.Text.Encoding]::UTF8)

    # Take the Open section only: from its heading until the next "## " heading. The
    # heading is matched by its word, not its emoji — Windows PowerShell 5.1 reads a
    # BOM-less UTF-8 script as ANSI, which corrupts an emoji literal in this file.
    $out    = New-Object System.Collections.Generic.List[string]
    $inOpen = $false
    foreach ($line in $lines) {
        if ($line -match '^##\s.*\bOpen\b')  { $inOpen = $true }
        elseif ($line.StartsWith('## '))     { $inOpen = $false }
        if ($inOpen) { $out.Add($line) }
    }

    Write-Output '=== backlog.md - Open issues (post-build reminder) ==='
    Write-Output ($out -join "`n")
}
catch {
    # Deliberately silent - see header.
}
