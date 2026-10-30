# check-rt-safety.ps1 — REPORT-ONLY scan for patterns that are suspicious on the audio thread.
#
# This is a cheap tripwire, not a proof. It reads source text, not call graphs: it cannot know which
# function runs inside processBlock. It therefore scans the files that make up the audio path
# (mu-core Audio/, Modulation/, Sequencer/, Control/, ProcessorBase*.cpp and the Plugin/ render, retire and transport helpers; every product's Source/Audio,
# Source/Sequencer, Source/Modulation and Source/Plugin/PluginProcessor*) and reports lines that match one
# of the rules below. Expect false positives; the dsp-controller reads the report, it decides nothing.
# The exit code is ALWAYS 0 (setup errors excepted: exit 2), so the script can run in /build without
# ever failing it. Promote it to a gate only after the findings have been triaged.
#
# Rules (a line matches when comments are blanked out):
#   alloc         `new T`, `delete p`, malloc / calloc / realloc / free
#   vector-grow   .push_back / .emplace_back / .resize / .reserve / .insert( / .setSize( on anything
#   lock          std::mutex / std::lock_guard / std::unique_lock / std::scoped_lock, juce::ScopedLock,
#                 ScopedReadLock / ScopedWriteLock, CriticalSection .enter() (a ScopedTryLock is the
#                 allowed pattern and is not matched)
#   string        juce::String construction (juce::String(...), `juce::String x...`, juce::String::),
#                 .toString(), std::string construction, std::to_string
#   str-concat    a std::string built with `+` from an id (`x.id + "_suffix"`): a heap temporary on every call
#   raw-param     getRawParameterValue( (an APVTS string lookup; resolve to an atomic pointer once)
#   std-function  constructing a std::function (declaration with an initialiser or a call-style
#                 construction); a `const std::function<...>&` parameter is not matched
#   map-index     operator[] on a std::map / std::unordered_map declared in the same file with a
#                 std::string or juce::String key (allocates on a miss, hashes a string every time)
#
# Heuristic limits, stated plainly:
#   - File-level, not function-level. For a .cpp, a hit is attributed to the nearest column-0 definition
#     `Class::name(` above it; when that name looks like setup (prepare*, release*, reset*, load*, init*,
#     setup*, set<Upper>*, build*, create*, clear*, resize*, allocate*, a constructor or destructor) the
#     hit is counted as "setup context" and not listed. Headers get no such help, so inline members of a
#     header are listed in full.
#   - A hit in a helper that only the message thread calls is a false positive; the reader judges.
#   - Strings and `#if` branches are not parsed. Member-function declarations returning juce::String and the
#     plug-in factory's `new X::PluginProcessor()` are recognised and skipped.
#   - Suppress a reviewed line by putting `rt-ok` (whole word, case-sensitive) anywhere on it, in a comment.
#   - Suppress a whole function with `// rt-ok-fn: <reason>` on the definition line or the line above it: every
#     hit inside the braced body counts as suppressed (.cpp and header alike). Use it for functions only the
#     message / host thread calls, and give a real reason.
#   - `// rt-ok-class: <reason>` on a class's opening line (or the line above) counts every hit inside the class
#     body as setup context (not listed). Use it only for a class whose members are all message-thread.
#   - Files that never run on the audio thread (preset / host-state / layers glue, serialisers, clip and table
#     headers, render-support glue; the $notAudio list) are not scanned at all.
#   - Setup names also include bind*, cache*, sync*FromParameters, sync*FromApvts, force*, completeSetup. Generic
#     apply* / update* are NOT setup names: they can run on the audio thread.
#   - A push_back / emplace_back with `capacity()` in the two lines above is treated as guarded growth.
#
# Usage:
#   pwsh tests/scripts/check-rt-safety.ps1                 # report for the repo
#   pwsh tests/scripts/check-rt-safety.ps1 -Detail         # list every hit, not a sample per rule
#   pwsh tests/scripts/check-rt-safety.ps1 -Root <dir>     # scan another tree with the same layout
#   pwsh tests/scripts/check-rt-safety.ps1 -Top 15         # files listed per rule (default 8)
#
# Exit code: 0 always (report-only); 2 = setup error (root missing).

param(
    [string]$Root,
    [int]$Top = 8,
    [switch]$Detail,
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'
try { [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false) } catch { }

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Root) { $Root = $repoRoot }
if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
    Write-Host "check-rt-safety: SETUP ERROR - root not found at $Root"
    exit 2
}
$Root = (Resolve-Path -LiteralPath $Root).Path

function Get-RelPath([string]$full) {
    $r = $full.Substring($Root.Length).TrimStart([char]92, '/')
    return $r.Replace([string][char]92, '/')
}

# Audio-path files: the directories and files described in the header comment. Absent folders are skipped.
$scanFiles = New-Object System.Collections.Generic.List[string]
function Add-Tree([string]$dir) {
    if (Test-Path -LiteralPath $dir -PathType Container) {
        Get-ChildItem -LiteralPath $dir -Recurse -File -Include '*.h', '*.cpp' | ForEach-Object { $scanFiles.Add($_.FullName) }
    }
}
foreach ($d in 'Audio', 'Modulation', 'Sequencer', 'Control') { Add-Tree (Join-Path $Root "mu-core/$d") }
foreach ($f in 'Plugin/TimedMidiOut.h', 'Plugin/TransportResolver.h', 'Plugin/HostTransport.h',
          'Plugin/ProcessorBase.cpp', 'Plugin/ProcessorBase_Render.cpp', 'Plugin/RetireBin.h', 'Plugin/LayerBlock.h',
          'Plugin/HotSwap.h', 'Plugin/ProductRender.h', 'Plugin/RenderSupport.h', 'Plugin/MidiClockSync.h') {
    $p = Join-Path $Root "mu-core/$f"
    if (Test-Path -LiteralPath $p -PathType Leaf) { $scanFiles.Add((Resolve-Path -LiteralPath $p).Path) }
}
foreach ($prod in (Get-ChildItem -LiteralPath $Root -Directory -Filter 'mu-*' | Where-Object { $_.Name -ne 'mu-core' -and $_.Name -notlike 'mu-control*' })) {
    foreach ($d in 'Audio', 'Modulation', 'Sequencer') { Add-Tree (Join-Path $prod.FullName "Source/$d") }
    $plug = Join-Path $prod.FullName 'Source/Plugin'
    if (Test-Path -LiteralPath $plug -PathType Container) {
        Get-ChildItem -LiteralPath $plug -File -Include 'PluginProcessor*.h', 'PluginProcessor*.cpp' | ForEach-Object { $scanFiles.Add($_.FullName) }
    }
}

# Files that never run on the audio thread (state serialisation, host-state, layer management, clip and
# preset plumbing, tables, render-support glue): dropped from the scan so they do not drown the report.
$notAudio = [regex]('(^|/)(PluginProcessor_(Preset|HostState|Layers)\.cpp|\w*Serialise\.h|StepRow\.h|ClipBank\.h|ClipClipboard\.h|ClipLibrary\w*\.\w+|LayerTypeRegistry\.h|FXAlgorithmDef\.h|MusicalScales\.h|MidiOutputMapping\.h|ChordVoicing\.h|StripMods\.h|ModulationAssignment\.h|ProductRender\.h|RenderSupport\.h)$|^mu-core/Control/MidiControlMap\.cpp$')
$kept = New-Object System.Collections.Generic.List[string]
foreach ($f in $scanFiles) { if (-not $notAudio.IsMatch((Get-RelPath $f))) { $kept.Add($f) } }
$scanFiles = $kept

$rules = @(
    @{ Id = 'alloc';        Re = [regex]'(?<![\w.>])new\s+[A-Za-z_:]|(?<![\w.>])delete\b\s*(\[\s*\])?\s*[A-Za-z_(]|\b(malloc|calloc|realloc|free)\s*\(' },
    @{ Id = 'vector-grow';  Re = [regex]'\.\s*(push_back|emplace_back|resize|reserve|setSize)\s*\(|\.\s*insert\s*\(' },
    @{ Id = 'lock';         Re = [regex]'\bstd::(mutex|recursive_mutex|lock_guard|unique_lock|scoped_lock|shared_mutex)\b|\bScoped(Read|Write)?Lock\b(?!Type)|\bScopedLock\b|\.enter\(\s*\)|\bGenericScopedLock\b' },
    @{ Id = 'string';       Re = [regex]'\bjuce::String\s*\(|\bjuce::String\s+\w+\s*(=|\(|\{|;)|\bjuce::String::|\.toString\s*\(\s*\)|\bstd::string\s+\w+\s*(=|\(|\{|;)|\bstd::to_string\b|\bString\s*\(\s*"' },
    @{ Id = 'raw-param';    Re = [regex]'\bgetRawParameterValue\s*\(' },
    @{ Id = 'str-concat';   Re = [regex]'\.\w*[iI]d\s*\+\s*"' },
    @{ Id = 'std-function'; Re = [regex]'\bstd::function\s*<[^;]*>\s+\w+\s*(=|\(|\{)|\bstd::function\s*<[^;]*>\s*[({]|\bstd::function\s*<[^;]*>\s*\w+\s*=\s*\[|\bmake_function\b' }
)
$mapIndex = 'map-index'
$setupName = [regex]'^(prepare\w*|release\w*|reset\w*|load\w*|init\w*|setup\w*|set[A-Z]\w*|build\w*|create\w*|clear\w*|resize\w*|allocate\w*|bind\w*|cache\w*|sync\w*FromParameters|sync\w*FromApvts|force\w*|completeSetup|~\w+)$'
$funcDef = [regex]'^(?=[A-Za-z_])[^;]*?\b(\w+)::(~?\w+)\s*\('
$blockComment = [regex]'(?s)/\*.*?\*/'
$blankBlock = [System.Text.RegularExpressions.MatchEvaluator] { param($m) $m.Value -replace '[^\r\n]+', '' }
$mapDecl = [regex]'\bstd::(unordered_)?map\s*<\s*(const\s+)?(std::string|juce::String)\s*,[^;]*?>\s+(\w+)\s*[;{=(]'

$hits = New-Object System.Collections.Generic.List[object]
$setupHits = @{}
$suppressed = 0

foreach ($path in ($scanFiles | Sort-Object -Unique)) {
    $raw     = [System.IO.File]::ReadAllText($path)
    $rawLines = $raw -split "\r?\n"
    $cleaned = ($blockComment.Replace($raw, $blankBlock) -replace '//[^\r\n]*', '') -split "\r?\n"
    $rel     = Get-RelPath $path
    $isCpp   = $path.EndsWith('.cpp')

    # Names of string-keyed maps declared anywhere in this file (for the map-index rule).
    $maps = @()
    foreach ($m in $mapDecl.Matches(($cleaned -join "`n"))) { $maps += $m.Groups[4].Value }
    $mapIdx = $null
    if ($maps.Count -gt 0) { $mapIdx = [regex]('(?<![\w.])(' + (($maps | Sort-Object -Unique) -join '|') + ')\s*\[') }

    # Function / class regions marked `rt-ok-fn:` / `rt-ok-class:` (see header): 1 = fn, 2 = class.
    $region = New-Object 'int[]' $cleaned.Count
    for ($k = 0; $k -lt $cleaned.Count; $k++) {
        $kind = 0
        if ($rawLines[$k] -cmatch '\brt-ok-fn:') { $kind = 1 } elseif ($rawLines[$k] -cmatch '\brt-ok-class:') { $kind = 2 }
        if ($kind -eq 0) { continue }
        # Walk from the marker to the end of the next braced body; a `;` before any `{` means no body.
        $depth = 0; $opened = $false; $end = -1
        for ($e = $k; $e -lt $cleaned.Count -and $end -lt 0; $e++) {
            $txt = ($cleaned[$e] -replace '"(\\.|[^"\\])*"', '""') -replace "'(\\.|[^'\\])'", "''"
            foreach ($ch in $txt.ToCharArray()) {
                if ($ch -eq '{') { $depth++; $opened = $true }
                elseif ($ch -eq '}') { $depth--; if ($opened -and $depth -le 0) { $end = $e; break } }
                elseif ($ch -eq ';' -and -not $opened) { $end = -2; break }
            }
            if ($end -eq -2) { break }
        }
        if ($end -ge 0) { for ($e = $k; $e -le $end; $e++) { if ($region[$e] -ne 1) { $region[$e] = $kind } } }
    }

    for ($i = 0; $i -lt $cleaned.Count; $i++) {
        $line = $cleaned[$i]
        if ($line.Trim().Length -eq 0) { continue }
        if ($line -match '^\s*#\s*include') { continue }
        $matched = @()
        foreach ($rule in $rules) {
            $test = $line
            if ($rule.Id -eq 'string') {
                # Empty construction does not allocate; drop it before testing.
                $test = $test -replace '\bjuce::String\s*\(\s*\)|\bstd::string\s*\{\s*\}', ''
                # A bare member / local declaration without initialiser (`juce::String id;`) does not allocate.
                if ($test -match '^\s*(const\s+|mutable\s+)*(juce::String|std::string)\s+\w+\s*(\{\s*\})?\s*;\s*$') { continue }
            }
            # A `using X = std::function<...>` alias constructs nothing.
            if ($rule.Id -eq 'std-function' -and $line -match '^\s*using\s+\w+\s*=') { continue }
            if (-not $rule.Re.IsMatch($test)) { continue }
            # A push_back after a capacity() check (within 2 lines above) is the guarded-growth pattern.
            if ($rule.Id -eq 'vector-grow' -and $line -match '\.\s*(push_back|emplace_back)\s*\(' -and
                (($i -ge 1 -and $cleaned[$i - 1] -match 'capacity\s*\(\s*\)') -or ($i -ge 2 -and $cleaned[$i - 2] -match 'capacity\s*\(\s*\)'))) { continue }
            # The plug-in factory's `new X::PluginProcessor()` is the one allowed allocation (message thread, once).
            if ($rule.Id -eq 'alloc' -and $line -match 'new\s+([\w]+::)*PluginProcessor\s*\(') { continue }
            # A member-function declaration returning juce::String is not a construction.
            if ($rule.Id -eq 'string' -and $line -match '\bjuce::String\s+\w+\s*\([^)]*\)\s*(const\b|override\b|noexcept\b|;\s*$)' -and $line -notmatch '=\s*juce::String|juce::String\s*\(') { continue }
            # So is a function definition starting inline / static / virtual whose signature ends the line or opens the body.
            if ($rule.Id -eq 'string' -and $line -match '^\s*((inline|static|virtual|constexpr)\s+)+(const\s+)?juce::String\s+\w+\s*\(' -and
                ($line -notmatch '\)' -or $line -match '\)\s*((const|override|noexcept)\s*)*(\{\s*)?$') -and $line -notmatch '=\s*juce::String|juce::String\s*\(') { continue }
            $matched += $rule.Id
        }
        if ($mapIdx -and $mapIdx.IsMatch($line)) { $matched += $mapIndex }
        if ($matched.Count -eq 0) { continue }
        if ($rawLines[$i] -cmatch '\brt-ok\b(?!-)') { $suppressed++; continue }
        if ($region[$i] -eq 1) { $suppressed++; continue }

        # Setup context: the nearest column-0 definition above the line, in a .cpp.
        $setup = $false
        $fn = ''
        if ($isCpp) {
            for ($j = $i; $j -ge 0; $j--) {
                $mm = $funcDef.Match($cleaned[$j])
                if ($mm.Success) {
                    $fn = $mm.Groups[1].Value + '::' + $mm.Groups[2].Value
                    $isCtor = $mm.Groups[1].Value -eq $mm.Groups[2].Value
                    $setup = $isCtor -or $setupName.IsMatch($mm.Groups[2].Value)
                    break
                }
            }
        }
        # `rt-ok-class:` members are counted as setup context, headers included.
        if ($region[$i] -eq 2) { $setup = $true }
        foreach ($id in $matched) {
            if ($setup) {
                $setupHits[$id] = 1 + [int]$setupHits[$id]
            } else {
                $snippet = $rawLines[$i].Trim()
                if ($snippet.Length -gt 110) { $snippet = $snippet.Substring(0, 110) }
                $hits.Add([pscustomobject]@{ Rule = $id; File = $rel; Line = $i + 1; Fn = $fn; Text = $snippet })
            }
        }
    }
}

if (-not $Quiet) {
    Write-Host "check-rt-safety: REPORT-ONLY (never fails). $($scanFiles.Count) audio-path files scanned."
    foreach ($rule in (@($rules | ForEach-Object { $_.Id }) + $mapIndex)) {
        $mine = @($hits | Where-Object { $_.Rule -eq $rule })
        $setupN = [int]$setupHits[$rule]
        Write-Host ""
        Write-Host ("[{0}] {1} hit(s) outside setup functions, {2} in setup context (not listed)" -f $rule, $mine.Count, $setupN)
        if ($mine.Count -eq 0) { continue }
        $byFile = $mine | Group-Object File | Sort-Object Count -Descending
        foreach ($g in ($byFile | Select-Object -First $Top)) { Write-Host ("    {0,4}  {1}" -f $g.Count, $g.Name) }
        if ($byFile.Count -gt $Top) { Write-Host ("    ... and {0} more file(s); use -Top or -Detail" -f ($byFile.Count - $Top)) }
        $sample = if ($Detail) { $mine } else { $mine | Select-Object -First 4 }
        foreach ($h in $sample) {
            $where = if ($h.Fn) { " [$($h.Fn)]" } else { '' }
            Write-Host ("      {0}:{1}{2}  {3}" -f $h.File, $h.Line, $where, $h.Text)
        }
    }
}
Write-Host ""
Write-Host "check-rt-safety: REPORT ($($hits.Count) finding(s) outside setup context, $((($setupHits.Values | Measure-Object -Sum).Sum)) in setup context, $suppressed suppressed by rt-ok) - exit 0 by design"
exit 0
