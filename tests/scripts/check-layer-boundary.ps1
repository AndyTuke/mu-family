# check-layer-boundary.ps1 — guard for the per-product layer libraries (mu-<product>-layer).
#
# The detachable-layer extraction (docs/mu-tronic/extraction-plan.md, section 4.5) moves each product's layer
# code (its sound engine, sequencer, modulation, persistence and layer UI) into an INTERFACE library next to
# mu-core: mu-clid-layer, mu-tant-layer, mu-toni-layer, mu-on-layer. A layer library is what a future
# standalone host links, so it must not reach back into a product shell. This script enforces the shape:
#
#   Rule 1  A layer-library file must not #include anything under Plugin/ or License/ (mu-core's Plugin/
#           included: the library depends on the Layer interface, never on the processor).
#   Rule 2  A layer-library file must not name PluginProcessor (comments blanked first), must not include a
#           product source tree (mu-clid/, mu-clid-midi/, mu-tant/, mu-toni/, mu-on/) and must not include
#           another layer library's headers (a library may include its own folder name).
#   Rule 3  Every relative path (relative to its library root) must be unique across mu-core and all layer
#           libraries, so an include "Audio/Foo.h" can never mean two files. Reported once per colliding pair.
#
# Rules from the plan that are NOT implemented yet (they need the registered types and the Published
# LayerAccess mode that only exist once E1 lands; each reports nothing today):
#   Rule 4  A global-namespace type in a library file.
#   Rule 5  "Glue": Plugin/*.cpp or PluginProcessor* defining render* / read*Config / applyModulation, or
#           naming voiceEngines / retiring for a registered type.
#   Rule 6  Layer reads outside LayerBlock (N5): in a product whose processor selects LayerAccess::Published,
#           a `layers[` or `layerAs<` read in processBlock, a shell post-stage or flagLayerSwapBoundaries.
#           TODO(E1): see Test-LayerReadsOutsideLayerBlock below, a documented stub that returns no finding.
#
# The scan is driven by -Root, a directory laid out like the repo root: mu-core/ and any mu-*-layer/
# folders in it. A layer library that does not exist yet is simply absent, so today the script scans
# mu-core, finds no library and passes. The fixture self-test (test-check-layer-boundary.ps1) points -Root at
# synthetic trees under fixtures/layer-boundary/.
#
#   pwsh tests/scripts/check-layer-boundary.ps1                 # check the repo
#   pwsh tests/scripts/check-layer-boundary.ps1 -Root <dir>     # check another tree
#   pwsh tests/scripts/check-layer-boundary.ps1 -Quiet          # summary line only
#
# Exit code 0 = clean, 1 = one or more violations, 2 = setup error (root missing).

param(
    [string]$Root,
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'

# Violation lines carry an em-dash: force UTF-8 so it survives redirection.
try { [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false) } catch { }

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Root) { $Root = $repoRoot }
if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
    Write-Host "check-layer-boundary: SETUP ERROR - root not found at $Root"
    exit 2
}
$Root = (Resolve-Path -LiteralPath $Root).Path.TrimEnd([char]92, '/')

# Libraries present under the root: mu-core (when it exists) and every mu-*-layer folder.
$libraries = @()
$core = Join-Path $Root 'mu-core'
if (Test-Path -LiteralPath $core -PathType Container) { $libraries += @{ Name = 'mu-core'; Dir = $core; IsLayer = $false } }
foreach ($d in (Get-ChildItem -LiteralPath $Root -Directory -Filter 'mu-*-layer' | Sort-Object Name)) {
    $libraries += @{ Name = $d.Name; Dir = $d.FullName; IsLayer = $true }
}

$blockComment = [regex]'(?s)/\*.*?\*/'
$blankBlock = [System.Text.RegularExpressions.MatchEvaluator] { param($m) $m.Value -replace '[^\r\n]+', '' }
$includePluginOrLicense = [regex]'#\s*include\s*[<"]([^">]*/)?(Plugin|License)/'
$includeProduct = [regex]'#\s*include\s*[<"]([^">]*/)?(mu-(clid-midi|clid|tant|toni|on))/'
$includeLayerLib = [regex]'#\s*include\s*[<"]([^">]*/)?(mu-[a-z-]+-layer)/'
$namesProcessor = [regex]'\bPluginProcessor\b'
$skipDir = [regex]'(^|/)(build[^/]*|cmake-build[^/]*|ThirdParty|_deps|\.git|_out)(/|$)'

function Get-LibraryFiles($lib) {
    $base = (Resolve-Path -LiteralPath $lib.Dir).Path.TrimEnd([char]92, '/')
    foreach ($f in (Get-ChildItem -LiteralPath $base -Recurse -File -Include '*.h', '*.hpp', '*.cpp' | Sort-Object FullName)) {
        $rel = $f.FullName.Substring($base.Length).TrimStart([char]92, '/').Replace([string][char]92, '/')
        if ($skipDir.IsMatch($rel)) { continue }
        [pscustomobject]@{ Full = $f.FullName; Rel = $rel; Lib = $lib.Name; IsLayer = $lib.IsLayer }
    }
}

# TODO(E1): rule 6 (N5). Once a product's processor selects LayerAccess::Published, this must report any
# `layers[` / `layerAs<` read in processBlock, a shell post-stage or flagLayerSwapBoundaries, with a
# `layer-ok` marker for audited lines. It needs to know which products have switched mode, which only
# exists after E1, so it deliberately finds nothing now.
function Test-LayerReadsOutsideLayerBlock([string]$root) { return @() }

$all = @()
foreach ($lib in $libraries) { $all += @(Get-LibraryFiles $lib) }

$fails = 0
function Report([string]$msg) {
    $script:fails++
    if (-not $Quiet) { Write-Host "  [FAIL] $msg" }
}

# Rules 1 and 2: scan each layer-library file with comments blanked out.
foreach ($file in ($all | Where-Object { $_.IsLayer })) {
    $text = [System.IO.File]::ReadAllText($file.Full)
    $cleaned = $blockComment.Replace($text, $blankBlock) -replace '//[^\r\n]*', ''
    $lineNo = 0
    foreach ($line in ($cleaned -split "\r?\n")) {
        $lineNo++
        $where = "$($file.Lib)/$($file.Rel):$lineNo"
        $snippet = $line.Trim()
        if ($snippet.Length -gt 90) { $snippet = $snippet.Substring(0, 90) }

        if ($includePluginOrLicense.IsMatch($line)) {
            Report "$where — a layer library includes a Plugin/ or License/ header (rule 1): $snippet"
        }
        if ($namesProcessor.IsMatch($line)) {
            Report "$where — a layer library names PluginProcessor (rule 2): $snippet"
        }
        if ($includeProduct.IsMatch($line)) {
            Report "$where — a layer library includes a product source tree (rule 2): $snippet"
        }
        $m = $includeLayerLib.Match($line)
        if ($m.Success -and $m.Groups[2].Value -ne $file.Lib) {
            Report "$where — a layer library includes another layer library's header (rule 2): $snippet"
        }
    }
}

# Rule 3: one relative path may exist in only one library.
$byRel = $all | Group-Object { $_.Rel.ToLowerInvariant() } | Where-Object { $_.Count -gt 1 }
foreach ($g in $byRel) {
    $owners = ($g.Group | ForEach-Object { "$($_.Lib)/$($_.Rel)" }) -join ' and '
    Report "$($g.Group[0].Rel) — the same relative path exists in more than one library (rule 3): $owners"
}

# Rule 6 stub: reports nothing until E1.
foreach ($finding in (Test-LayerReadsOutsideLayerBlock $Root)) { Report $finding }

$layerCount = @($libraries | Where-Object { $_.IsLayer }).Count
$status = if ($fails -eq 0) { 'PASS' } else { 'FAIL' }
Write-Host "check-layer-boundary: $status ($($all.Count) files in $($libraries.Count) libraries, $layerCount layer librar$(if ($layerCount -eq 1) { 'y' } else { 'ies' }), $fails violation(s))"
exit $(if ($fails -eq 0) { 0 } else { 1 })
