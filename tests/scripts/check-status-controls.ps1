# check-status-controls.ps1 - the status-bar hard rule, static half.
#
# "The status bar shows the name and value of the last control touched, in every app, without fail." The ui-audit apps
# (run-ui-audits.ps1) prove it on the real editors; this check is the cheap net under them: every UI source file that handles mouse
# input (mouseDown / mouseUp / mouseDrag) must either report to the status bar or say why it need not.
#
# A file passes when it (or its .h / .cpp twin) mentions reportStatus, onStatusUpdate, showParam, StatusSink or setStatusName, or
# carries a "status-ok: <reason>" comment (a display, an overlay, a meter: something that is not a control).
#
#   pwsh tests/scripts/check-status-controls.ps1
param([string]$Root = (Split-Path $PSScriptRoot -Parent | Split-Path -Parent))

$ErrorActionPreference = 'Stop'
$dirs = @('mu-core/UI') + (Get-ChildItem $Root -Directory -Filter 'mu-*' | Where-Object { $_.Name -ne 'mu-core' } | ForEach-Object { "$($_.Name)/Source" })
$reports = 'reportStatus|onStatusUpdate|showParam|StatusSink|setStatusName|status-ok:'

$bad = @()
foreach ($d in $dirs) {
    $path = Join-Path $Root $d
    if (-not (Test-Path $path)) { continue }
    foreach ($h in Get-ChildItem $path -Recurse -Include *.h -File | Where-Object { $_.FullName -notmatch '\\Tests\\' }) {
        $text = Get-Content $h.FullName -Raw
        if ($text -notmatch 'void\s+mouse(Down|Up|Drag)\s*\(') { continue }
        $twin = [IO.Path]::ChangeExtension($h.FullName, '.cpp')
        $all = $text + $(if (Test-Path $twin) { Get-Content $twin -Raw } else { '' })
        if ($all -notmatch $reports) { $bad += $h.FullName.Substring($Root.Length + 1) }
    }
}

if ($bad.Count -gt 0) {
    Write-Host 'check-status-controls: FAIL - these handle the mouse but neither report to the status bar nor say why not:'
    $bad | ForEach-Object { Write-Host "  $_" }
    Write-Host 'Report with mu_ui::reportStatus(*this, name, value), or add a "// status-ok: <reason>" comment if it is not a control.'
    exit 1
}
Write-Host 'check-status-controls: PASS'
