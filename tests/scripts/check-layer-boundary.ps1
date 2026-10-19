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
#   Rule 4  A layer-library file must not define a type (class / struct / union / enum) in the global
#           namespace: four libraries share one include path and one link in a multi-layer host, so every
#           library type lives in its product's namespace. Forward declarations of mu-core's own global types
#           (`struct Layer;`, `class VoiceEngine;`) are not definitions and pass.
#   Rule 5  "Glue" (D1): for a REGISTERED product (one whose mu-<product>-layer library exists, or named in
#           -Registered), a file under mu-<product>/Source/Plugin/ that is a .cpp or named PluginProcessor*
#           must not DEFINE a function named render*, read*Config or applyModulation, nor name the identifiers
#           voiceEngines or retiring: that per-layer render glue belongs in the layer. Calls (renderAndMix(...);)
#           are not definitions. Coarse by design; comments are blanked first. The standalone shell's
#           render-mode hooks listed in $shellRenderHooks (renderPlaysByDefault, a mu-core StandaloneShell
#           override) are app policy, not layer glue, and are skipped.
#   Rule 6  Layer reads outside LayerBlock (N5): in a product whose Plugin/ sources select
#           LayerAccess::Published, any line of its Plugin/*.cpp or PluginProcessor* files reading `layers[` or
#           `layerAs<` is reported unless the line carries a `layer-ok` marker (with a reason) that a reviewer
#           wrote after auditing it (message-thread code, or a value cached by renderLayers). Coarse by design:
#           it fires on the file, the reviewer clears audited lines. Live from the first Published host (T1).
#   Rule 7  Every live-count store goes through ProcessorCore::storeLayerCount, which keeps the packed
#           count | serial layout word in step: `numLayers.store` may appear only in mu-core/Plugin/ProcessorCore.h.
#           Scans every mu-* source tree (mu-core and the products); Tests/ folders are exempt (test fixtures
#           build states by hand).
#   Rule 8  mu-core's Audio/, Modulation/, Persistence/ and Sequencer/ never include Plugin/ (the layer side of
#           mu-core stays below the processor side; RetireBin and LayerBlock live in Plugin/ for that reason).
#   Rule 9  A layer library declared in a product's CMakeLists.txt lists only layer sources: neither its
#           target_sources(mu-<p>-layer INTERFACE ...) block nor its MU_<P>_LAYER_UI_SOURCES list may name a file
#           under Plugin/, License/ or Tests/, the app's SettingsOverlay or PluginEditor. A layer library file also
#           must not include PluginEditor.h or SettingsOverlay.h (the app's own window and settings page; rule 2).
#
# The scan is driven by -Root, a directory laid out like the repo root: mu-core/, any mu-*-layer/ libraries and
# the product trees mu-*/Source/. A layer library is either its own mu-<p>-layer/ folder or, as the products
# build them (docs/mu-tronic/design.md 2.5, "no folder moves"), declared in the product's own CMakeLists.txt as
# add_library(mu-<p>-layer INTERFACE ...): its files are then the product's Source/Audio, Sequencer,
# Modulation, Persistence and UI folders, without UI/SettingsOverlay.* (Plugin/, License/ and Tests/ are the
# app). A layer library that does not exist yet is simply absent. The fixture self-test
# (test-check-layer-boundary.ps1) points -Root at synthetic trees under fixtures/layer-boundary/.
#
#   pwsh tests/scripts/check-layer-boundary.ps1                 # check the repo
#   pwsh tests/scripts/check-layer-boundary.ps1 -Root <dir>     # check another tree
#   pwsh tests/scripts/check-layer-boundary.ps1 -Registered toni,on   # treat these products as registered (rule 5)
#   pwsh tests/scripts/check-layer-boundary.ps1 -Quiet          # summary line only
#
# Exit code 0 = clean, 1 = one or more violations, 2 = setup error (root missing).

param(
    [string]$Root,
    [string[]]$Registered = @(),
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
if (Test-Path -LiteralPath $core -PathType Container) { $libraries += @{ Name = 'mu-core'; Dir = $core; IsLayer = $false; InTree = $false } }
foreach ($d in (Get-ChildItem -LiteralPath $Root -Directory -Filter 'mu-*-layer' | Sort-Object Name)) {
    $libraries += @{ Name = $d.Name; Dir = $d.FullName; IsLayer = $true; InTree = $false }
}
# Layer libraries a product declares over its own Source/ folders.
$inTreeLibrary = [regex]'(?m)^[ \t]*add_library\s*\(\s*(?<name>mu-[a-z-]+-layer)\s+INTERFACE\b'
foreach ($d in (Get-ChildItem -LiteralPath $Root -Directory -Filter 'mu-*' | Sort-Object Name)) {
    if ($d.Name -eq 'mu-core' -or $d.Name -like 'mu-*-layer') { continue }
    $cmake  = Join-Path $d.FullName 'CMakeLists.txt'
    $source = Join-Path $d.FullName 'Source'
    if (-not (Test-Path -LiteralPath $cmake -PathType Leaf) -or -not (Test-Path -LiteralPath $source -PathType Container)) { continue }
    foreach ($m in $inTreeLibrary.Matches([System.IO.File]::ReadAllText($cmake))) {
        if ($m.Groups['name'].Value -ne ($d.Name + '-layer')) { continue }
        $libraries += @{ Name = $m.Groups['name'].Value; Dir = $source; IsLayer = $true; InTree = $true }
    }
}

$blockComment = [regex]'(?s)/\*.*?\*/'
$blankBlock = [System.Text.RegularExpressions.MatchEvaluator] { param($m) $m.Value -replace '[^\r\n]+', '' }
$includePluginOrLicense = [regex]'#\s*include\s*[<"]([^">]*/)?(Plugin|License)/'
$includeProduct = [regex]'#\s*include\s*[<"]([^">]*/)?(mu-(clid-midi|clid|tant|toni|on))/'
$includeLayerLib = [regex]'#\s*include\s*[<"]([^">]*/)?(mu-[a-z-]+-layer)/'
$namesProcessor = [regex]'\bPluginProcessor\b'
$includeAppUi = [regex]'#\s*include\s*[<"]([^">]*/)?(PluginEditor|SettingsOverlay)\.h[">]'
$skipDir = [regex]'(^|/)(build[^/]*|cmake-build[^/]*|ThirdParty|_deps|\.git|_out)(/|$)'
# An in-tree library's files: the layer folders of the product's Source/, without the app's settings page.
$inTreeFolders = [regex]'^(Audio|Sequencer|Modulation|Persistence|UI)/'
$inTreeExcluded = [regex]'^UI/SettingsOverlay\.'

function Get-LibraryFiles($lib) {
    $base = (Resolve-Path -LiteralPath $lib.Dir).Path.TrimEnd([char]92, '/')
    foreach ($f in (Get-ChildItem -LiteralPath $base -Recurse -File -Include '*.h', '*.hpp', '*.cpp' | Sort-Object FullName)) {
        $rel = $f.FullName.Substring($base.Length).TrimStart([char]92, '/').Replace([string][char]92, '/')
        if ($skipDir.IsMatch($rel)) { continue }
        if ($lib.InTree -and (-not $inTreeFolders.IsMatch($rel) -or $inTreeExcluded.IsMatch($rel))) { continue }
        [pscustomobject]@{ Full = $f.FullName; Rel = $rel; Lib = $lib.Name; IsLayer = $lib.IsLayer }
    }
}

# Comments, string and character literals blanked (newlines kept, so line numbers stay true).
function Get-CleanedText([string]$text) {
    $t = $blockComment.Replace($text, $blankBlock)
    $t = [regex]::Replace($t, '"(?:\\.|[^"\\\r\n])*"', '""')
    $t = [regex]::Replace($t, "'(?:\\.|[^'\\\r\n])+'", "''")
    $t = $t -replace '//[^\r\n]*', ''
    return $t
}

function Get-LineNumber([string]$text, [int]$index) {
    return ([regex]::Matches($text.Substring(0, $index), "`n")).Count + 1
}

# Rule 4: type definitions at global scope. A brace stack tells namespace bodies (allowed to hold types)
# from every other brace; a type definition found with an empty stack is global.
$scopeToken = [regex]'(?<ns>\bnamespace\b(?:\s+[A-Za-z_][\w:]*)?\s*\{)|(?<type>\b(?<kw>class|struct|union|enum)\b(?:\s+(?:class|struct))?\s+(?:alignas\s*\([^)]*\)\s*)?(?<name>[A-Za-z_]\w*)(?:\s+final)?\s*(?::[^;{}()]*)?\{)|(?<open>\{)|(?<close>\})'
function Test-GlobalTypes($file) {
    $text = [System.IO.File]::ReadAllText($file.Full)
    $cleaned = (Get-CleanedText $text) -replace '(?m)^[ \t]*#[^\r\n]*', ''
    $stack = New-Object System.Collections.Generic.List[string]
    $found = @()
    foreach ($m in $scopeToken.Matches($cleaned)) {
        if ($m.Groups['ns'].Success)      { $stack.Add('ns') }
        elseif ($m.Groups['type'].Success) {
            if ($stack.Count -eq 0) {
                $line = Get-LineNumber $cleaned $m.Index
                $found += "$($file.Lib)/$($file.Rel):$line — a layer library defines a global-namespace type '$($m.Groups['name'].Value)' (rule 4)"
            }
            $stack.Add('type')
        }
        elseif ($m.Groups['open'].Success) { $stack.Add('other') }
        elseif ($stack.Count -gt 0)        { $stack.RemoveAt($stack.Count - 1) }
    }
    return $found
}

# The product trees under the root (mu-<name>/Source/Plugin), excluding mu-core and the layer libraries.
function Get-ProductPluginFiles([string]$root) {
    $out = @()
    foreach ($d in (Get-ChildItem -LiteralPath $root -Directory -Filter 'mu-*' | Sort-Object Name)) {
        if ($d.Name -eq 'mu-core' -or $d.Name -like 'mu-*-layer') { continue }
        $plugin = Join-Path $d.FullName 'Source/Plugin'
        if (-not (Test-Path -LiteralPath $plugin -PathType Container)) { continue }
        foreach ($f in (Get-ChildItem -LiteralPath $plugin -File -Include '*.h', '*.hpp', '*.cpp' -Recurse | Sort-Object FullName)) {
            $isGlueCandidate = $f.Extension -eq '.cpp' -or $f.Name -like 'PluginProcessor*'
            $rel = $f.FullName.Substring($d.FullName.Length).TrimStart([char]92, '/').Replace([string][char]92, '/')
            $out += [pscustomobject]@{ Product = $d.Name; Full = $f.FullName; Rel = $rel; Candidate = $isGlueCandidate }
        }
    }
    return $out
}

# Rule 5: per-layer render glue left in a registered product's shell.
$glueDefinition = [regex]'(?m)^[ \t]*(?:[\w:<>,]+[ \t\*&]+)+(?:\w+::)*(?<fn>render\w*|read\w*Config|applyModulation)\s*\((?<args>[^;{}]*)\)\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?(?:final\s*)?\{'
$glueIdentifier = [regex]'\b(voiceEngines|retiring)\b'
# The standalone shell's render-mode hooks (mu-core Plugin/StandaloneShell.h) are app policy, not layer glue.
$shellRenderHooks = @('renderPlaysByDefault')
function Test-Glue($files, [string[]]$registeredProducts) {
    $found = @()
    foreach ($f in ($files | Where-Object { $_.Candidate -and $registeredProducts -contains $_.Product })) {
        $cleaned = Get-CleanedText ([System.IO.File]::ReadAllText($f.Full))
        foreach ($m in $glueDefinition.Matches($cleaned)) {
            if ($shellRenderHooks -contains $m.Groups['fn'].Value) { continue }
            $line = Get-LineNumber $cleaned $m.Index
            $found += "$($f.Product)/$($f.Rel):$line — a registered product's shell defines layer glue '$($m.Groups['fn'].Value)' (rule 5)"
        }
        foreach ($m in $glueIdentifier.Matches($cleaned)) {
            $line = Get-LineNumber $cleaned $m.Index
            $found += "$($f.Product)/$($f.Rel):$line — a registered product's shell names '$($m.Value)' (rule 5)"
        }
    }
    return $found
}

# Rule 6 (N5): a product whose processor selects LayerAccess::Published reads layers only through LayerBlock.
$publishedMode = [regex]'LayerAccess::Published'
$layerRead = [regex]'\blayers\s*\[|\blayerAs\s*<'
function Test-LayerReadsOutsideLayerBlock($files) {
    $found = @()
    $byProduct = $files | Group-Object Product
    foreach ($g in $byProduct) {
        $published = $false
        foreach ($f in $g.Group) {
            if ($publishedMode.IsMatch((Get-CleanedText ([System.IO.File]::ReadAllText($f.Full))))) { $published = $true; break }
        }
        if (-not $published) { continue }
        foreach ($f in ($g.Group | Where-Object { $_.Candidate })) {
            $text = [System.IO.File]::ReadAllText($f.Full)
            $origLines = $text -split "\r?\n"
            $cleanLines = (Get-CleanedText $text) -split "\r?\n"
            for ($i = 0; $i -lt $cleanLines.Count; $i++) {
                if (-not $layerRead.IsMatch($cleanLines[$i])) { continue }
                if ($i -lt $origLines.Count -and $origLines[$i] -match '\blayer-ok\b') { continue }
                $snippet = $origLines[$i].Trim()
                if ($snippet.Length -gt 90) { $snippet = $snippet.Substring(0, 90) }
                $found += "$($f.Product)/$($f.Rel):$($i + 1) — a Published-mode product reads a layer outside LayerBlock (rule 6; audited lines carry layer-ok): $snippet"
            }
        }
    }
    return $found
}

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
        if ($includeAppUi.IsMatch($line)) {
            Report "$where — a layer library includes the app's editor or settings page (rule 2): $snippet"
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

# Rule 4: global-namespace types in layer-library files.
foreach ($file in ($all | Where-Object { $_.IsLayer })) {
    foreach ($finding in (Test-GlobalTypes $file)) { Report $finding }
}

# Rules 5 and 6 read the product shells. A product is registered when its layer library exists (or -Registered names it).
$productFiles = @(Get-ProductPluginFiles $Root)
$registeredProducts = @()
foreach ($lib in ($libraries | Where-Object { $_.IsLayer })) { $registeredProducts += ($lib.Name -replace '-layer$', '') }
foreach ($r in $Registered) { foreach ($p in ($r -split ',')) { if ($p.Trim()) { $registeredProducts += ('mu-' + ($p.Trim() -replace '^mu-', '')) } } }
foreach ($finding in (Test-Glue $productFiles $registeredProducts)) { Report $finding }
foreach ($finding in (Test-LayerReadsOutsideLayerBlock $productFiles)) { Report $finding }

# Rule 7: a count store outside storeLayerCount leaves the layout word stale.
$countStore = [regex]'\bnumLayers\s*\.\s*store\s*\('
foreach ($d in (Get-ChildItem -LiteralPath $Root -Directory -Filter 'mu-*' | Sort-Object Name)) {
    $base = $d.FullName
    foreach ($f in (Get-ChildItem -LiteralPath $base -Recurse -File -Include '*.h', '*.hpp', '*.cpp' | Sort-Object FullName)) {
        $rel = $f.FullName.Substring($base.Length).TrimStart([char]92, '/').Replace([string][char]92, '/')
        if ($skipDir.IsMatch($rel) -or $rel -match '(^|/)Tests/') { continue }
        if ($d.Name -eq 'mu-core' -and $rel -eq 'Plugin/ProcessorCore.h') { continue }
        $cleaned = Get-CleanedText ([System.IO.File]::ReadAllText($f.FullName))
        foreach ($m in $countStore.Matches($cleaned)) {
            $line = Get-LineNumber $cleaned $m.Index
            Report "$($d.Name)/$($rel):$line — a live-count store outside storeLayerCount leaves the layout word stale (rule 7)"
        }
    }
}

# Rule 8: mu-core's layer-side folders never reach up into Plugin/.
if (Test-Path -LiteralPath $core -PathType Container) {
    foreach ($file in ($all | Where-Object { $_.Lib -eq 'mu-core' -and $_.Rel -match '^(Audio|Modulation|Persistence|Sequencer)/' })) {
        # Comments blanked only: the include path is a string literal, which Get-CleanedText would empty.
        $cleaned = $blockComment.Replace([System.IO.File]::ReadAllText($file.Full), $blankBlock) -replace '//[^\r\n]*', ''
        foreach ($m in ([regex]'#\s*include\s*[<"]([^">]*/)?Plugin/[^">]*[">]').Matches($cleaned)) {
            $line = Get-LineNumber $cleaned $m.Index
            Report "mu-core/$($file.Rel):$line — mu-core's $($file.Rel.Split('/')[0])/ includes a Plugin/ header (rule 8)"
        }
    }
}

# Rule 9: the source lists of a product-declared layer library name only layer files.
$appSourcePath = [regex]'(?i)(^|[/\\])(Plugin|License|Tests)[/\\]|SettingsOverlay|PluginEditor'
foreach ($lib in ($libraries | Where-Object { $_.IsLayer -and $_.InTree })) {
    $product = $lib.Name -replace '-layer$', ''
    $cmakeText = [System.IO.File]::ReadAllText((Join-Path (Join-Path $Root $product) 'CMakeLists.txt'))
    $cmakeText = [regex]::Replace($cmakeText, '(?m)#[^\r\n]*', '')
    $blocks = @()
    $targetBlock = [regex]('(?s)target_sources\s*\(\s*' + [regex]::Escape($lib.Name) + '\s+INTERFACE(?<body>.*?)\)')
    foreach ($m in $targetBlock.Matches($cmakeText)) { $blocks += @{ Where = "target_sources($($lib.Name))"; Body = $m.Groups['body'].Value; Index = $m.Index } }
    $uiVar = 'MU_' + (($product -replace '^mu-', '').ToUpperInvariant()) + '_LAYER_UI_SOURCES'
    $uiBlock = [regex]('(?s)set\s*\(\s*' + $uiVar + '(?<body>.*?)\)')
    foreach ($m in $uiBlock.Matches($cmakeText)) { $blocks += @{ Where = $uiVar; Body = $m.Groups['body'].Value; Index = $m.Index } }
    foreach ($b in $blocks) {
        foreach ($tok in ([regex]'[^\s"()]+\.(?:cpp|h|hpp)').Matches($b.Body)) {
            if ($appSourcePath.IsMatch($tok.Value)) {
                $line = Get-LineNumber $cmakeText ($b.Index + $b.Body.IndexOf($tok.Value))
                Report "$product/CMakeLists.txt:$line — $($b.Where) lists an app source '$($tok.Value)' (rule 9)"
            }
        }
    }
}

$layerCount = @($libraries | Where-Object { $_.IsLayer }).Count
$status = if ($fails -eq 0) { 'PASS' } else { 'FAIL' }
Write-Host "check-layer-boundary: $status ($($all.Count) files in $($libraries.Count) libraries, $layerCount layer librar$(if ($layerCount -eq 1) { 'y' } else { 'ies' }), $fails violation(s))"
exit $(if ($fails -eq 0) { 0 } else { 1 })
