# check-release-set.ps1 — is every product's full set of formats present for a config?
#
# The expected set per product (owner, 2026-11-19):
#   mu-Clid, mu-Tant, mu-Toni, mu-On, mu-Tilate : VST3 + CLAP + Standalone
#   mu-Tronic                                   : Standalone only
#   mu-Host, mu-Clid MIDI, mu-Toni MIDI         : VST3 + CLAP only (plug-ins)
#   mu-Link                                     : mu-link.exe
#
#   pwsh tests/scripts/check-release-set.ps1                 # Release
#   pwsh tests/scripts/check-release-set.ps1 -Config Debug
#
# Also fails when an artefact is older than build\mu-core's BuildNumber.h stamp would allow
# (older than the newest file of the same config by more than -MaxSkewHours), so a stale
# half-rebuilt set is caught too. Exit 0 = complete, 1 = something missing or stale.

param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Release',
    [double]$MaxSkewHours = 6
)

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$build = Join-Path $repo 'build'

# slug, file stem, formats
$set = @(
    @{ Slug = 'mu-clid';      Name = 'mu-Clid';      Formats = 'VST3', 'CLAP', 'Standalone' },
    @{ Slug = 'mu-tant';      Name = 'mu-Tant';      Formats = 'VST3', 'CLAP', 'Standalone' },
    @{ Slug = 'mu-toni';      Name = 'mu-Toni';      Formats = 'VST3', 'CLAP', 'Standalone' },
    @{ Slug = 'mu-on';        Name = 'mu-On';        Formats = 'VST3', 'CLAP', 'Standalone' },
    @{ Slug = 'mu-tilate';    Name = 'mu-Tilate';    Formats = 'VST3', 'CLAP', 'Standalone' },
    @{ Slug = 'mu-tronic';    Name = 'mu-Tronic';    Formats = 'Standalone' },
    @{ Slug = 'mu-host';      Name = 'mu-Host';      Formats = 'VST3', 'CLAP' },
    @{ Slug = 'mu-clid-midi'; Name = 'mu-Clid MIDI'; Formats = 'VST3', 'CLAP' },
    @{ Slug = 'mu-toni-midi'; Name = 'mu-Toni MIDI'; Formats = 'VST3', 'CLAP' }
)

$missing = @()
$files = @()
foreach ($p in $set) {
    $root = Join-Path $build "$($p.Slug)\$($p.Slug)_artefacts\$Config"
    foreach ($f in $p.Formats) {
        $ext = @{ VST3 = 'vst3'; CLAP = 'clap'; Standalone = 'exe' }[$f]
        $path = Join-Path $root "$f\$($p.Name).$ext"
        if (Test-Path -LiteralPath $path) { $files += Get-Item -LiteralPath $path } else { $missing += "$($p.Name) $f" }
    }
}
$link = Join-Path $build "mu-link\mu-link_artefacts\$Config\mu-link.exe"
if (Test-Path -LiteralPath $link) { $files += Get-Item -LiteralPath $link } else { $missing += 'mu-Link exe' }

$stale = @()
if ($files.Count -gt 0) {
    $newest = ($files | Measure-Object LastWriteTime -Maximum).Maximum
    foreach ($f in $files) {
        if (($newest - $f.LastWriteTime).TotalHours -gt $MaxSkewHours) { $stale += "$($f.Name) ($($f.LastWriteTime))" }
    }
}

if ($missing.Count -eq 0 -and $stale.Count -eq 0) {
    Write-Host "check-release-set ($Config): PASS - all $($files.Count) artefacts present and current"
    exit 0
}
if ($missing.Count) { Write-Host "check-release-set ($Config): FAIL - missing:"; $missing | ForEach-Object { Write-Host "  $_" } }
if ($stale.Count)   { Write-Host "check-release-set ($Config): FAIL - stale (older than the rest of the set):"; $stale | ForEach-Object { Write-Host "  $_" } }
exit 1
