# PreToolUse hook for `git commit`: run the fast static checks and block the commit (exit 2)
# when one fails. Only runs the checks relevant to the files this commit will contain.
#
# The hook runs BEFORE the command, so in `git add x && git commit` nothing is staged yet.
# The files in scope are therefore: already-staged files, plus changed files whose path appears
# in the command text, plus everything changed when the command uses `add -A`, `add .` or `commit -a`.
$ErrorActionPreference = 'Continue'
$root = $env:CLAUDE_PROJECT_DIR
if (-not $root) { exit 0 }
Set-Location $root

# The command text arrives as JSON on stdin (tool_input.command).
$cmd = ''
try { $cmd = ([Console]::In.ReadToEnd() | ConvertFrom-Json).tool_input.command } catch { }
if (-not $cmd) { $cmd = '' }

$changed = @(git status --porcelain 2>$null | ForEach-Object { $_.Substring(3).Trim('"') -replace '^.* -> ', '' })
if ($changed.Count -eq 0) { exit 0 }

$everything = $cmd -match 'add\s+(-A|--all|\.)(\s|$)' -or $cmd -match 'commit\s+(.*\s)?-[a-zA-Z]*a'
$staged = @(git diff --cached --name-only 2>$null)
$scope = if ($everything) { $changed }
         else { @($staged) + @($changed | Where-Object { $_ -and $cmd.Contains($_) }) }
$scope = @($scope | Select-Object -Unique)
if ($scope.Count -eq 0) { exit 0 }

$failures = @()

function Run-Check([string]$name, [string[]]$extra) {
    $out = & pwsh -NonInteractive -File "tests/scripts/$name" @extra 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) { $script:failures += "$name failed:`n$out" }
}

# Source comment rules and the mu-core boundary apply when C++ sources are in the commit.
if ($scope | Where-Object { $_ -match '\.(h|hpp|cpp|mm)$' }) {
    Run-Check 'check-comment-refs.ps1' @('-Quiet')
    Run-Check 'check-core-boundary.ps1' @()
}
# JUCE pin check when the pin or the top-level CMake file is in the commit.
if ($scope | Where-Object { $_ -match '^(juce-version\.txt|CMakeLists\.txt)$' }) {
    Run-Check 'check-juce-version.ps1' @()
}
# Backlog structure when the backlog is in the commit.
if ($scope | Where-Object { $_ -eq 'backlog.md' }) {
    Run-Check 'check-backlog.ps1' @()
}

if ($failures.Count -gt 0) {
    [Console]::Error.WriteLine(($failures -join "`n"))
    exit 2   # exit 2 blocks the tool call and feeds stderr back to Claude
}
exit 0
