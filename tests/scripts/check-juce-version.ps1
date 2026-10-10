# check-juce-version.ps1 — report how far the family's JUCE pin is behind JUCE's latest release.
#
# The family tracks the latest stable JUCE (CLAUDE.md, "JUCE is kept up to date"). The pin lives
# in juce-version.txt at the repo root; CMake checks the local checkout against it and the CI
# workflows check out that tag. This script asks GitHub for the newest JUCE release and says
# whether a bump is due. It needs the gh CLI (authenticated) and network access.
#
#   pwsh tests/scripts/check-juce-version.ps1
#
# Exit code 0 = pin is current, 1 = a newer JUCE release exists, 2 = setup error (no pin / no gh / offline).

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$pinFile  = Join-Path $repoRoot 'juce-version.txt'
if (-not (Test-Path -LiteralPath $pinFile)) {
    Write-Host "check-juce-version: SETUP ERROR - $pinFile not found"
    exit 2
}
$pin = (Get-Content -LiteralPath $pinFile -Raw).Trim()

try {
    $latest = (& gh api repos/juce-framework/JUCE/releases/latest --jq .tag_name 2>$null)
    if ($LASTEXITCODE -ne 0 -or -not $latest) { throw 'gh api failed' }
    $latest = $latest.Trim()
} catch {
    Write-Host "check-juce-version: SETUP ERROR - could not query the latest JUCE release (is gh installed and logged in?)"
    exit 2
}

# Compare as versions; tags are plain x.y.z.
$pinV = [version]$pin
$latV = [version]$latest
if ($pinV -ge $latV) {
    Write-Host "check-juce-version: CURRENT (pin $pin, latest release $latest)"
    exit 0
}

# How far behind: list the release tags newer than the pin.
$newer = @(& gh api 'repos/juce-framework/JUCE/releases?per_page=50' --jq '.[] | select(.prerelease == false) | .tag_name' 2>$null |
    Where-Object { $_ -match '^\d+\.\d+\.\d+$' -and [version]$_ -gt $pinV } |
    Sort-Object { [version]$_ })
$gap = if ($latV.Major -gt $pinV.Major) { 'major' } elseif ($latV.Minor -gt $pinV.Minor) { 'minor' } else { 'patch' }
Write-Host "check-juce-version: BEHIND (pin $pin, latest release $latest — $gap gap, $($newer.Count) release(s) newer: $($newer -join ', '))"
Write-Host "  To bump: read JUCE's BREAKING_CHANGES.md from $pin to $latest, edit juce-version.txt, check out the tag at JUCE_PATH, rebuild and test."
exit 1
