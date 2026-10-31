# deploy-vst3.ps1 - copy the Release VST3 bundles into the system VST3 folder (owner rule, 2026-10-31).
# Run after every Release build. Only Release builds are deployed; Debug never is.
#   pwsh tests/scripts/deploy-vst3.ps1
# Exit 0 when every bundle copied, 1 when a copy failed (usually a DAW holding the plugin open).

param(
    [string]$Dest = 'C:\Program Files\Common Files\VST3'
)

$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$bundles = Get-ChildItem (Join-Path $root 'build') -Directory -Recurse -Filter '*.vst3' |
    Where-Object { $_.FullName -match '\\Release\\VST3\\[^\\]+\.vst3$' }

if (-not $bundles) { Write-Host 'deploy-vst3: no Release VST3 bundles found (build Release first)'; exit 1 }
if (-not (Test-Path $Dest)) { Write-Host "deploy-vst3: $Dest does not exist"; exit 1 }

$failed = 0
foreach ($b in $bundles) {
    $target = Join-Path $Dest $b.Name
    # /MIR makes the installed bundle identical to the build output (removes files that no longer exist).
    robocopy $b.FullName $target /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { Write-Host "deploy-vst3: FAILED  $($b.Name)"; $failed++ }
    else                     { Write-Host "deploy-vst3: ok      $($b.Name)" }
}
exit ([int]($failed -gt 0))
