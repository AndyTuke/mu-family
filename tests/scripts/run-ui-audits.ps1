# run-ui-audits.ps1 - the status-bar hard rule, checked on every app's real editor.
#
# "The status bar shows the name and value of the last control touched, in every app, without fail." Each product has a
# <product>-ui-audit console app (mu-core/Tests/UiAudit/UiAuditMain.cpp, built by mu_add_ui_audit in cmake/UiAudit.cmake) that builds
# the product's REAL processor and editor, changes every knob, number box and dropdown it can find (stepping through each engine /
# mode too) and lists any that left the status bar empty. This script builds them and runs them all; it fails if any control is silent.
#
#   pwsh tests/scripts/run-ui-audits.ps1                    # build + run all nine
#   pwsh tests/scripts/run-ui-audits.ps1 -Products mu-on    # just one
#   pwsh tests/scripts/run-ui-audits.ps1 -NoBuild           # run what is built
#
# A new control reports by (a) being a knob (it reports through the shell), (b) having a status name (DropdownSelect::setStatusName /
# NudgeInput::setStatusName), or (c) calling its content's onStatusUpdate. See mu-core/UI/StatusSink.h.
param(
    [string[]]$Products = @('mu-clid', 'mu-tant', 'mu-toni', 'mu-on', 'mu-tilate', 'mu-tronic', 'mu-host', 'mu-toni-midi', 'mu-clid-midi'),
    [string]$Config = 'Debug',
    [string]$BuildDir = (Join-Path (Split-Path $PSScriptRoot -Parent | Split-Path -Parent) 'build'),
    [switch]$NoBuild
)

$ErrorActionPreference = 'Stop'

if (-not $NoBuild) {
    $targets = $Products | ForEach-Object { "$_-ui-audit" }
    Write-Host "Building: $($targets -join ' ')"
    & cmake --build $BuildDir --config $Config --target @targets
    if ($LASTEXITCODE -ne 0) { throw 'build failed' }
}

$failed = @()
foreach ($p in $Products) {
    $exe = Join-Path $BuildDir "$p-ui-audit_artefacts\$Config\$p-ui-audit.exe"
    if (-not (Test-Path $exe)) { throw "$exe not found - build it first" }
    & $exe $p
    if ($LASTEXITCODE -ne 0) { $failed += $p }
}

if ($failed.Count -gt 0) {
    Write-Host "ui-audit FAIL: controls that do not report to the status bar in: $($failed -join ', ')"
    exit 1
}
Write-Host 'ui-audit PASS: every control in every app reports to the status bar'
