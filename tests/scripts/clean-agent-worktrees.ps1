# Deletes the build folder inside each agent worktree under .claude/worktrees so finished agents do not
# fill the drive (a full mu build is 1 to 11 GB per worktree). Source files and git history are never touched.
#
#   pwsh tests/scripts/clean-agent-worktrees.ps1            list what would be deleted
#   pwsh tests/scripts/clean-agent-worktrees.ps1 -Apply     delete
#   -KeepRecentHours 2    skip worktrees whose build folder changed in the last N hours (an agent may be using it)
#   -RemoveWorktrees      also remove worktrees that are clean and whose branch is merged into main
param(
    [switch]$Apply,
    [int]$KeepRecentHours = 2,
    [switch]$RemoveWorktrees
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$root = Join-Path $repo '.claude\worktrees'
if (-not (Test-Path $root)) { Write-Host "No agent worktrees under $root"; exit 0 }

function Get-FolderGB([string]$path) {
    [math]::Round((Get-ChildItem -LiteralPath $path -Recurse -File -Force -ErrorAction SilentlyContinue |
        Measure-Object Length -Sum).Sum / 1GB, 2)
}

$freed = 0.0
foreach ($wt in Get-ChildItem -LiteralPath $root -Directory) {
    $build = Join-Path $wt.FullName 'build'
    if (-not (Test-Path -LiteralPath $build)) { continue }

    # A junction (build redirected to another drive) is removed as a link, never followed.
    $isLink = ((Get-Item -LiteralPath $build -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
    $newest = (Get-ChildItem -LiteralPath $build -Recurse -File -Force -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1).LastWriteTime
    if (-not $isLink -and $newest -and $newest -gt (Get-Date).AddHours(-$KeepRecentHours)) {
        Write-Host ("skip   {0}  (build changed within {1} h; an agent may be using it)" -f $wt.Name, $KeepRecentHours)
        continue
    }
    $gb = if ($isLink) { 0 } else { Get-FolderGB $build }
    if ($Apply) {
        if ($isLink) { cmd /c rmdir "$build" | Out-Null } else { Remove-Item -LiteralPath $build -Recurse -Force -ErrorAction SilentlyContinue }
        Write-Host ("delete {0}  {1} GB" -f $wt.Name, $gb)
    } else {
        Write-Host ("would delete {0}  {1} GB" -f $wt.Name, $gb)
    }
    $freed += $gb
}

if ($RemoveWorktrees) {
    foreach ($wt in Get-ChildItem -LiteralPath $root -Directory) {
        $branch = (git -C $wt.FullName branch --show-current 2>$null)
        $dirty = (git -C $wt.FullName status --porcelain 2>$null | Where-Object { $_ -notmatch 'ThirdParty/clap-juce|build/' } | Measure-Object).Count
        $merged = $branch -and ((git -C $repo branch --merged main --format='%(refname:short)') -contains $branch)
        # A branch still at main's tip has no work of its own yet: it may be an agent that has just started.
        $atMain = $branch -and ((git -C $repo rev-parse $branch) -eq (git -C $repo rev-parse main))
        if ($dirty -eq 0 -and $merged -and -not $atMain) {
            if ($Apply) { git -C $repo worktree remove --force $wt.FullName; Write-Host "removed worktree $($wt.Name) ($branch)" }
            else { Write-Host "would remove worktree $($wt.Name) ($branch, merged, clean)" }
        }
    }
}

Write-Host ("{0} {1:N1} GB" -f $(if ($Apply) { 'freed' } else { 'reclaimable' }), $freed)
if (-not $Apply) { Write-Host "Add -Apply to delete." }
