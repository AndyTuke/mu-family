# check-core-boundary.ps1 — guard: mu-core must not depend on any plugin.
#
# mu-core is the shared INTERFACE library every plugin links; the dependency has to be
# strictly one-way (plugin -> mu-core, never the reverse). A core file that pulls in a
# plugin header or names a plugin symbol breaks reuse — it's the "looks generic but is
# secretly plugin-coupled" trap. A core->plugin dependency *requires* an #include, so the
# include check is the load-bearing one; the namespace check is belt-and-braces.
#
# Comments are stripped before matching, so a doc reference to "PluginProcessor" or
# "mu-clid" in a comment is fine — only real code dependencies fail.
#
#   pwsh tests/scripts/check-core-boundary.ps1            # check mu-core at the repo root
#   pwsh tests/scripts/check-core-boundary.ps1 -Quiet     # summary line only
#
# Exit code 0 = clean, 1 = one or more violations, 2 = setup error (mu-core missing).

param(
    [string]$Path,
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'

# Violation lines carry an em-dash. Force UTF-8 out so it survives redirection into a CI
# log or a pipe through a non-UTF-8 shell, where it would otherwise arrive as "?".
try { [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false) } catch { }

# Resolve mu-core relative to the repo root (this script lives in tests/scripts/).
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Path) { $Path = Join-Path $repoRoot 'mu-core' }

if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
    Write-Host "check-core-boundary: SETUP ERROR - mu-core not found at $Path"
    exit 2
}

# Report paths relative to the repo root, leaving a -Path outside the repo absolute.
function Get-RepoRelativePath([string]$full) {
    if ($full.StartsWith($repoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        $full = $full.Substring($repoRoot.Length).TrimStart('\', '/')
    }
    return $full.Replace('\', '/')
}

$includePlugin = [regex]'#\s*include\s*[<"][^">]*\bmu-(clid|tant|toni)\b'
$pluginNs      = [regex]'\bmu_(clid|tant|toni)::'
$blockComment  = [regex]'(?s)/\*.*?\*/'

# Blank a block comment but keep its newlines, so line numbers after it stay accurate.
$blankBlock = [System.Text.RegularExpressions.MatchEvaluator] {
    param($m)
    $m.Value -replace '[^\r\n]+', ''
}

$files = Get-ChildItem -LiteralPath $Path -Recurse -File -Include '*.h', '*.cpp' | Sort-Object FullName
$fails = 0

# Scan every mu-core source line for a plugin include or a plugin namespace reference.
foreach ($file in $files) {
    $text    = [System.IO.File]::ReadAllText($file.FullName)
    $cleaned = $blockComment.Replace($text, $blankBlock) -replace '//.*', ''
    $rel     = Get-RepoRelativePath $file.FullName

    $lineNo = 0
    foreach ($line in ($cleaned -split "\r?\n")) {
        $lineNo++
        $snippet = $line.Trim()
        if ($snippet.Length -gt 90) { $snippet = $snippet.Substring(0, 90) }

        if ($includePlugin.IsMatch($line)) {
            if (-not $Quiet) { Write-Host "  [FAIL] ${rel}:${lineNo} — mu-core includes a plugin header: $snippet" }
            $fails++
        }
        if ($pluginNs.IsMatch($line)) {
            if (-not $Quiet) { Write-Host "  [FAIL] ${rel}:${lineNo} — mu-core references a plugin namespace: $snippet" }
            $fails++
        }
    }
}

$status = if ($fails -eq 0) { 'PASS' } else { 'FAIL' }
Write-Host "check-core-boundary: $status ($($files.Count) mu-core files, $fails violation(s))"
exit $(if ($fails -eq 0) { 0 } else { 1 })
