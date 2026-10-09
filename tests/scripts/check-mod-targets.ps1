# check-mod-targets.ps1 — guard: every product follows the family modulation-target
# standard (mu-core Modulation/ModTarget.h, docs/design-plugin-family.md "Modulation
# targets").
#
# The rule: depth is a percentage of the target knob's range, and each product defines its
# targets in ONE table of mu_mod::ModTarget rows. This fails if a product
#   - brings back per-target depth scales (registerDepthScale / depthScale... anywhere), or
#   - defines its own target-row struct instead of using mu_mod::ModTarget, or
#   - has no target table built from mu_mod::ModTarget at all.
#
# Comments are stripped before matching, so prose that mentions the old names is fine.
#
#   pwsh tests/scripts/check-mod-targets.ps1          # check every product
#   pwsh tests/scripts/check-mod-targets.ps1 -Quiet   # summary line only
#
# Exit code 0 = clean, 1 = one or more violations, 2 = setup error (a tree is missing).

param(
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'

try { [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false) } catch { }

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$products = @('mu-clid', 'mu-tant', 'mu-toni', 'mu-on')

$scaleApi   = [regex]'\b(registerDepthScale|depthScaleFor|depthScaleRegistry)\b'
$ownRow     = [regex]'\bstruct\s+(ModDest|ModDestEntry|Dest|ModTargetRow)\s*\{'
$usesTarget = [regex]'\bmu_mod::ModTarget\b'
$blockComment = [regex]'(?s)/\*.*?\*/'

# Blank a block comment but keep its newlines, so line numbers after it stay accurate.
$blankBlock = [System.Text.RegularExpressions.MatchEvaluator] {
    param($m)
    $m.Value -replace '[^\r\n]+', ''
}

function Get-RepoRelativePath([string]$full) {
    if ($full.StartsWith($repoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        $full = $full.Substring($repoRoot.Length).TrimStart('\', '/')
    }
    return $full.Replace('\', '/')
}

function Get-Sources([string]$root) {
    return Get-ChildItem -LiteralPath $root -Recurse -File -Include '*.h', '*.cpp' | Sort-Object FullName
}

function Get-CodeWithoutComments([string]$path) {
    $text = [System.IO.File]::ReadAllText($path)
    return $blockComment.Replace($text, $blankBlock) -replace '//.*', ''
}

$fails = 0

# mu-core keeps no scale registry either.
$core = Join-Path $repoRoot 'mu-core'
if (-not (Test-Path -LiteralPath $core -PathType Container)) {
    Write-Host "check-mod-targets: SETUP ERROR - mu-core not found at $core"
    exit 2
}
foreach ($file in Get-Sources $core) {
    if ($scaleApi.IsMatch((Get-CodeWithoutComments $file.FullName))) {
        if (-not $Quiet) { Write-Host "  [FAIL] $(Get-RepoRelativePath $file.FullName) - per-target depth scales are gone; depth is % of the knob range" }
        $fails++
    }
}

# Each product: no scale API, no home-grown target row, and one real ModTarget table.
foreach ($product in $products) {
    $root = Join-Path $repoRoot (Join-Path $product 'Source')
    if (-not (Test-Path -LiteralPath $root -PathType Container)) {
        Write-Host "check-mod-targets: SETUP ERROR - $root not found"
        exit 2
    }

    $hasTable = $false
    foreach ($file in Get-Sources $root) {
        $code = Get-CodeWithoutComments $file.FullName
        $rel  = Get-RepoRelativePath $file.FullName

        if ($scaleApi.IsMatch($code)) {
            if (-not $Quiet) { Write-Host "  [FAIL] $rel - per-target depth scale; add a table row instead (depth = % of range)" }
            $fails++
        }
        if ($ownRow.IsMatch($code)) {
            if (-not $Quiet) { Write-Host "  [FAIL] $rel - own modulation-target struct; use mu_mod::ModTarget" }
            $fails++
        }
        # The table has to live in the product's Modulation/ folder, as the standard says.
        if ($file.FullName.Split([char[]]@('\', '/')) -contains 'Modulation' -and $usesTarget.IsMatch($code)) {
            $hasTable = $true
        }
    }

    if (-not $hasTable) {
        if (-not $Quiet) { Write-Host "  [FAIL] $product - no modulation target table of mu_mod::ModTarget rows found" }
        $fails++
    }
}

$status = if ($fails -eq 0) { 'PASS' } else { 'FAIL' }
Write-Host "check-mod-targets: $status ($($products.Count) products, $fails violation(s))"
exit $(if ($fails -eq 0) { 0 } else { 1 })
