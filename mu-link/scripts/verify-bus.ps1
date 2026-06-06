# verify-bus.ps1 — automated audio verification of the mu-link bus.
#
# Drives mu-link-harness (a headless "virtual mu-link" that renders the bus to WAV and
# self-asserts on frequency + level) through a set of scenarios and aggregates PASS/FAIL.
# No audio hardware required, so it runs anywhere. A real mu-link.exe must NOT be running
# (it would own the shared-memory names the harness needs).
#
#   pwsh mu-link/scripts/verify-bus.ps1            # run scenarios against current build
#   pwsh mu-link/scripts/verify-bus.ps1 -Build     # cmake build the tools first
#   pwsh mu-link/scripts/verify-bus.ps1 -Config Release
#
# Exit code 0 = all scenarios passed.

param(
    [switch]$Build,
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"
$repo = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$artefacts = Join-Path $repo "build\mu-link"
$harness = Join-Path $artefacts "mu-link-harness_artefacts\$Config\mu-link-harness.exe"
$tone    = Join-Path $artefacts "mu-link-tone_artefacts\$Config\mu-link-tone.exe"
$outDir  = Join-Path $repo "build\harness-wavs"

if ($Build) {
    Write-Host "Building mu-link-harness + mu-link-tone ($Config)..." -ForegroundColor Cyan
    cmake --build (Join-Path $repo "build") --config $Config --target mu-link-harness mu-link-tone | Out-Host
    if ($LASTEXITCODE -ne 0) { Write-Host "Build failed." -ForegroundColor Red; exit 1 }
}

foreach ($exe in @($harness, $tone)) {
    if (-not (Test-Path $exe)) {
        Write-Host "Missing: $exe`nRun with -Build first." -ForegroundColor Red
        exit 1
    }
}
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

# Each scenario: a label + the harness argument list. The harness exits 0 on PASS.
$scenarios = @(
    @{ Name = "in-process single tone (440 Hz)";
       Args = @("--internal-tone","440","--seconds","2","--expect-hz","440",
                "--out",(Join-Path $outDir "tone440.wav")) },
    @{ Name = "in-process chord (440 + 660 Hz)";
       Args = @("--internal-tone","440","--internal-tone","660","--seconds","2",
                "--expect-hz","440","--expect-hz","660",
                "--out",(Join-Path $outDir "chord.wav")) },
    @{ Name = "cross-process: two spawned mu-link-tone clients";
       Args = @("--spawn","$tone 440","--spawn","$tone 660","--seconds","2",
                "--expect-hz","440","--expect-hz","660",
                "--out",(Join-Path $outDir "spawned.wav")) }
)

$failures = 0
foreach ($s in $scenarios) {
    Write-Host "`n--- $($s.Name) ---" -ForegroundColor Cyan
    & $harness @($s.Args) | Out-Host
    if ($LASTEXITCODE -ne 0) { $failures++; Write-Host "  -> FAILED" -ForegroundColor Red }
    else                     { Write-Host "  -> passed"  -ForegroundColor Green }
}

Write-Host "`n=================================" -ForegroundColor Cyan
if ($failures -eq 0) {
    Write-Host "verify-bus: ALL $($scenarios.Count) SCENARIOS PASSED" -ForegroundColor Green
    Write-Host "WAVs in: $outDir"
    exit 0
} else {
    Write-Host "verify-bus: $failures of $($scenarios.Count) SCENARIOS FAILED" -ForegroundColor Red
    exit 1
}
