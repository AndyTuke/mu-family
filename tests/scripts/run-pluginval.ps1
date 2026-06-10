# run-pluginval.ps1 — headless validation of the mu-family VST3 plugins.
#
# Drives Tracktion's pluginval (https://github.com/Tracktion/pluginval) over every built
# product VST3 and aggregates PASS/FAIL. pluginval instantiates each plugin in a real host
# harness and hammers it: parameter fuzz, audio at many sample-rates / buffer-sizes, editor
# open/close, multiple instances, and a save->restore-state identity check (which directly
# exercises the family "Everything in APVTS" rule — an un-persisted param fails restore).
#
# Coverage: VST3 only (pluginval validates VST3 + AU, NOT CLAP / Standalone). All formats
# wrap the same ProcessorBase, so VST3 covers the shared engine; CLAP-wrapper bugs need a
# separate clap-validator pass (tracked as a later phase).
#
#   pwsh tests/scripts/run-pluginval.ps1                       # validate Release VST3s
#   pwsh tests/scripts/run-pluginval.ps1 -Config Debug         # validate Debug VST3s (CI uses this)
#   pwsh tests/scripts/run-pluginval.ps1 -Strictness 8         # crank the test depth (1..10)
#   pwsh tests/scripts/run-pluginval.ps1 -SkipGui              # skip editor tests (headless runners)
#
# pluginval is auto-downloaded to build/tools/pluginval on first run; override with
# $env:PLUGINVAL_PATH pointing at an existing pluginval executable.
#
# Exit code 0 = every present VST3 passed. Products that weren't built are skipped (the
# build step is the existence gate), not failed.

param(
    [string]$Config = "Release",
    [ValidateRange(1, 10)][int]$Strictness = 5,
    [switch]$SkipGui,
    [string]$PluginvalVersion = "v1.0.3"
)

$ErrorActionPreference = "Stop"
$repo = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$build = Join-Path $repo "build"

# The product VST3 artefact folders (each holds one <Name>.vst3 bundle). Mirrors the paths
# in check-build-artifacts.py. Absent ones are skipped — CI builds a subset (no lite/toni).
$vst3Dirs = @(
    "mu-clid/mu-clid_artefacts/$Config/VST3",
    "mu-clid/mu-clid-lite_artefacts/$Config/VST3",
    "mu-tant/mu-tant_artefacts/$Config/VST3",
    "mu-toni/mu-toni_artefacts/$Config/VST3",
    "mu-on/mu-on_artefacts/$Config/VST3"
)

# --- Locate (or fetch) the pluginval executable -----------------------------------------
function Get-Pluginval {
    if ($env:PLUGINVAL_PATH -and (Test-Path $env:PLUGINVAL_PATH)) {
        return (Resolve-Path $env:PLUGINVAL_PATH).Path
    }

    # Per-platform release asset + the executable's path within the unpacked zip.
    if ($IsWindows -or $null -eq $IsWindows) { $asset = "pluginval_Windows.zip"; $rel = "pluginval.exe" }
    elseif ($IsMacOS)                        { $asset = "pluginval_macOS.zip";   $rel = "pluginval.app/Contents/MacOS/pluginval" }
    else                                     { $asset = "pluginval_Linux.zip";   $rel = "pluginval" }

    $toolDir = Join-Path $build "tools/pluginval/$PluginvalVersion"
    $exe = Join-Path $toolDir $rel
    if (Test-Path $exe) { return $exe }

    Write-Host "Downloading pluginval $PluginvalVersion ($asset)..." -ForegroundColor Cyan
    New-Item -ItemType Directory -Force -Path $toolDir | Out-Null
    $zip = Join-Path $toolDir $asset
    $url = "https://github.com/Tracktion/pluginval/releases/download/$PluginvalVersion/$asset"
    Invoke-WebRequest -Uri $url -OutFile $zip
    Expand-Archive -Path $zip -DestinationPath $toolDir -Force
    if (-not (Test-Path $exe)) { throw "pluginval not found at '$exe' after unpacking $asset" }
    if (-not $IsWindows -and $null -ne $IsWindows) { chmod +x $exe }
    return $exe
}

$pluginval = Get-Pluginval
Write-Host "Using pluginval: $pluginval" -ForegroundColor DarkGray

# --- Validate each present VST3 ----------------------------------------------------------
$baseArgs = @("--strictness-level", "$Strictness", "--validate-in-process")
if ($SkipGui) { $baseArgs += "--skip-gui-tests" }

$passed = 0; $failed = 0; $skipped = 0
foreach ($dir in $vst3Dirs) {
    $vst3Folder = Join-Path $build $dir
    $bundle = $null
    if (Test-Path $vst3Folder) {
        $bundle = Get-ChildItem -Path $vst3Folder -Filter *.vst3 | Select-Object -First 1
    }
    if (-not $bundle) {
        Write-Host "  [skip] $dir (not built)" -ForegroundColor DarkYellow
        $skipped++
        continue
    }

    Write-Host "`n--- validating $($bundle.Name) ---" -ForegroundColor Cyan
    & $pluginval @baseArgs $bundle.FullName | Out-Host
    if ($LASTEXITCODE -eq 0) { $passed++; Write-Host "  -> passed" -ForegroundColor Green }
    else                     { $failed++; Write-Host "  -> FAILED (exit $LASTEXITCODE)" -ForegroundColor Red }
}

Write-Host "`n=================================" -ForegroundColor Cyan
Write-Host "run-pluginval: $passed passed, $failed failed, $skipped skipped (strictness $Strictness, $Config)" `
    -ForegroundColor $(if ($failed -eq 0) { "Green" } else { "Red" })
exit $(if ($failed -eq 0) { 0 } else { 1 })
