# check-build-artifacts.ps1 — post-build artefact guard.
#
# Two regression guards that don't need audio:
#   1. UTF-8 plugin-name integrity: every built mu-Clid binary must carry the display
#      name with a real UTF-8 mu (CE BC) "µ-Clid" and must NOT contain a mangled
#      "?-Clid" — the exact symptom when the /utf-8 MSVC flag is lost and the
#      -D PLUGIN_NAME define round-trips through the system code page.
#   2. Build-number source consistency: build_number.txt must equal BUILD_NUMBER in
#      mu-core/BuildNumber.h, so the deployed artefacts can't disagree about their
#      version.
#
#   pwsh tests/scripts/check-build-artifacts.ps1                  # Release artefacts
#   pwsh tests/scripts/check-build-artifacts.ps1 -Config Debug    # Debug artefacts
#   pwsh tests/scripts/check-build-artifacts.ps1 -VersionOnly     # skip the binary scan
#
# -VersionOnly runs the build-number check alone, so it is usable on a machine with no
# build tree (the review-only laptop) instead of failing as a setup error.
#
# Exit code 0 = all checks pass, 1 = any failure, 2 = setup error (no artefacts found).

param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Release',
    [switch]$VersionOnly
)

$ErrorActionPreference = 'Stop'

# The µ is the whole point of check 1. Force UTF-8 out so it survives redirection into a
# CI log or a pipe through a non-UTF-8 shell, where it would otherwise arrive as "?".
try { [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false) } catch { }

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

# ISO-8859-1 maps every byte to exactly one char, so a binary can be searched as a string
# without the encoder mangling non-text bytes.
$latin1 = [System.Text.Encoding]::GetEncoding(28591)
$goodMu = $latin1.GetString([byte[]] @(0xCE, 0xBC)) + '-Clid'   # real UTF-8 mu
$badMu  = '?-Clid'                                              # mangled mu

function Get-RepoRelativePath([string]$full) {
    if ($full.StartsWith($repoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        $full = $full.Substring($repoRoot.Length).TrimStart('\', '/')
    }
    return $full.Replace('\', '/')
}

# Every built mu-Clid binary across both targets and all three formats. VST3 is a bundle
# directory on Windows, so its payload is found by recursing rather than globbing one level.
function Get-Binaries([string]$config) {
    $base  = Join-Path $repoRoot (Join-Path 'build' 'mu-clid')
    if (-not (Test-Path -LiteralPath $base -PathType Container)) { return @() }

    $found = [System.Collections.Generic.List[string]]::new()
    foreach ($target in 'mu-clid_artefacts', 'mu-clid-lite_artefacts') {
        $root = Join-Path $base (Join-Path $target $config)
        if (-not (Test-Path -LiteralPath $root -PathType Container)) { continue }

        foreach ($flat in @(@{ Dir = 'Standalone'; Ext = '*.exe' }, @{ Dir = 'CLAP'; Ext = '*.clap' })) {
            $dir = Join-Path $root $flat.Dir
            if (Test-Path -LiteralPath $dir -PathType Container) {
                Get-ChildItem -LiteralPath $dir -File -Filter $flat.Ext |
                    ForEach-Object { $found.Add($_.FullName) }
            }
        }

        $vst3 = Join-Path $root 'VST3'
        if (Test-Path -LiteralPath $vst3 -PathType Container) {
            Get-ChildItem -LiteralPath $vst3 -File -Filter '*.vst3' -Recurse |
                ForEach-Object { $found.Add($_.FullName) }
        }
    }
    return $found | Sort-Object
}

# build_number.txt is the single source of truth; BuildNumber.h is generated from it at
# configure time, so a mismatch means the tree was built before the last bump.
function Test-BuildNumber {
    $txtFile = Join-Path $repoRoot 'build_number.txt'
    $hdrFile = Join-Path $repoRoot (Join-Path 'mu-core' 'BuildNumber.h')

    foreach ($f in $txtFile, $hdrFile) {
        if (-not (Test-Path -LiteralPath $f -PathType Leaf)) {
            return @{ Ok = $false; Detail = "missing $(Get-RepoRelativePath $f)" }
        }
    }

    $txt = ([System.IO.File]::ReadAllText($txtFile)).Trim()
    $hdr = [System.IO.File]::ReadAllText($hdrFile)
    $m   = [regex]::Match($hdr, '#define\s+BUILD_NUMBER\s+(\d+)')
    $hdrNum = if ($m.Success) { $m.Groups[1].Value } else { '(not found)' }

    return @{ Ok = ($txt -eq $hdrNum); Detail = "build_number.txt=$txt BuildNumber.h=$hdrNum" }
}

$fails = 0

$bn = Test-BuildNumber
Write-Host "  [$(if ($bn.Ok) { 'OK' } else { 'FAIL' })] build-number consistency: $($bn.Detail)"
if (-not $bn.Ok) { $fails++ }

if ($VersionOnly) {
    $status = if ($fails -eq 0) { 'PASS' } else { 'FAIL' }
    Write-Host "check-build-artifacts: $status (version check only, $fails failure(s))"
    exit $(if ($fails -eq 0) { 0 } else { 1 })
}

$binaries = @(Get-Binaries $Config)
if ($binaries.Count -eq 0) {
    Write-Host "check-build-artifacts: SETUP ERROR - no $Config artefacts under build/ -- run ``cmake --build build --config $Config`` first (or pass -VersionOnly)"
    exit 2
}

# Each binary must carry the real UTF-8 mu and never the mangled form.
foreach ($path in $binaries) {
    $hay  = $latin1.GetString([System.IO.File]::ReadAllBytes($path))
    $good = $hay.Contains($goodMu)
    $bad  = $hay.Contains($badMu)
    $ok   = $good -and (-not $bad)
    if (-not $ok) { $fails++ }

    $note = if ($good) { 'µ-Clid OK' } else { 'MISSING µ-Clid' }
    if ($bad) { $note += ' + FOUND mangled ?-Clid (regression!)' }
    Write-Host "  [$(if ($ok) { 'OK' } else { 'FAIL' })] $(Get-RepoRelativePath $path) : $note"
}

$status = if ($fails -eq 0) { 'PASS' } else { 'FAIL' }
Write-Host "check-build-artifacts: $status ($($binaries.Count) binaries, $fails failure(s))"
exit $(if ($fails -eq 0) { 0 } else { 1 })
