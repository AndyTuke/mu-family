# check-backlog.ps1 — structural guard for backlog.md.
#
# The backlog is the project's development history and is written by three independent
# authors: this repo's sessions, the owner by hand, and the sync-backlog CI workflow. The
# CLAUDE.md rules that keep it readable (grouped Open -> On Hold -> Closed, descending
# within On Hold and Closed, Open in development order, one resolved status) are prose, so nothing caught the drift that had
# accumulated by v1.0.950: two transposed rows, a pair stranded eight places out of order,
# five different "done" spellings, and — worst — two of the three tables missing their
# delimiter row, which silently stops GitHub rendering them as tables at all.
#
# Checks, in report order:
#   1. Sections     — Open / On Hold / Closed all present, exactly once, in that order.
#   2. Tables       — each section has a header row followed immediately by a delimiter
#                     row, and no orphan delimiter is stranded mid-table.
#   3. Order        — issue numbers strictly descending within On Hold and Closed (Open is in
#                     recommended development order, so it is exempt).
#   4. Duplicates   — an issue number appears once across the whole file.
#   5. Status       — the Status cell matches its section (Closed rows say "Closed", not
#                     "Fixed" / "Audited" / "Verified" / any other variant).
#   6. Columns      — every row has exactly 4 cells. A literal "|" in a description (from
#                     a shell snippet like `... | tee log`) splits the row into extra
#                     columns and pushes Status/Closed Build out of place. Backticks do
#                     NOT protect pipes in GitHub tables — only "\|" does.
#   7. Tally        — the "Tally (updated DATE): Open N · On Hold N · Closed N · Total N" line
#                     under "# Issues" must carry the real row counts. The line is generated:
#                     run with -FixTally after editing rows (it rewrites only that line, and
#                     only when a number is wrong, so repeated runs are no-ops). Two PCs
#                     editing the backlog no longer conflict on hand-edited counts.
#   8. Conflicts    — no git conflict markers (<<<<<<<, =======, >>>>>>> at line start).
#
#   pwsh tests/scripts/check-backlog.ps1                  # check backlog.md at the repo root
#   pwsh tests/scripts/check-backlog.ps1 -Path other.md   # check a different file
#   pwsh tests/scripts/check-backlog.ps1 -Quiet           # summary line only
#   pwsh tests/scripts/check-backlog.ps1 -FixTally        # regenerate the tally line, then check
#   pwsh tests/scripts/check-backlog.ps1 -SelfTest        # test the tally/conflict behaviours on temp copies
#
# Exit code 0 = clean, 1 = one or more violations, 2 = setup error (file missing/unreadable).

param(
    [string]$Path,
    [switch]$Quiet,
    [switch]$FixTally,
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'

# The status cells are emoji. Force UTF-8 out so they survive redirection into a CI log or
# a pipe through a non-UTF-8 shell, where they would otherwise arrive as "?".
try { [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false) } catch { }

# The tally line, matched on one line (no line ending). "·" is U+00B7, written as an escape
# so this script does not depend on how it is decoded.
$tallyRegex = '\*\*Tally \(updated (\d{4}-\d{2}-\d{2})\): Open (\d+) · On Hold (\d+) · Closed (\d+) · Total (\d+)\*\*'

# ── Self-test: runs this script against temp copies, never the real backlog ──────
if ($SelfTest) {
    $open   = [char]::ConvertFromUtf32(0x1F534) + ' Open'
    $hold   = [char]::ConvertFromUtf32(0x1F7E1) + ' On Hold'
    $closed = [char]::ConvertFromUtf32(0x2705)  + ' Closed'
    $dot    = [char]0x00B7
    $today  = Get-Date -Format 'yyyy-MM-dd'

    # Build a small valid backlog (Open 1, On Hold 1, Closed 2) with a given tally and EOL.
    function New-Backlog([string]$Tally, [string]$Eol, [string]$Extra = '') {
        $l = @('# Issues', '', $Tally, '',
               "## $open", '', '| # | Description | Status | Closed Build |', '|---|---|---|---|',
               "| 4 | d | $open | |", '',
               "## $hold", '', '| # | Description | Status | Closed Build |', '|---|---|---|---|',
               "| 3 | d | $hold | |", '',
               "## $closed", '', '| # | Description | Status | Closed Build |', '|---|---|---|---|',
               "| 2 | d | $closed | 1 |", "| 1 | d | $closed | 1 |")
        if ($Extra) { $l += $Extra }
        ($l -join $Eol) + $Eol
    }
    $good    = "**Tally (updated 2020-01-01): Open 1 $dot On Hold 1 $dot Closed 2 $dot Total 4**"
    $badTal  = "**Tally (updated 2020-01-01): Open 9 $dot On Hold 1 $dot Closed 2 $dot Total 4**"
    $fixedTl = "**Tally (updated ${today}): Open 1 $dot On Hold 1 $dot Closed 2 $dot Total 4**"
    $utf8    = New-Object System.Text.UTF8Encoding($false)

    $tmp = Join-Path ([IO.Path]::GetTempPath()) ("check-backlog-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force $tmp | Out-Null
    $script:bad = 0; $script:n = 0
    function Run([string]$Name, [string]$Content, [string[]]$ScriptArgs, [int]$WantExit, [string]$WantOut, [string]$WantFile) {
        $script:n++
        $f = Join-Path $tmp "$Name.md"
        [IO.File]::WriteAllBytes($f, $utf8.GetBytes($Content))
        $out = & pwsh -NoProfile -File $PSCommandPath -Path $f @ScriptArgs 2>&1 | Out-String
        $got = $LASTEXITCODE
        $after = $utf8.GetString([IO.File]::ReadAllBytes($f))
        $ok = ($got -eq $WantExit) -and (-not $WantOut -or $out -match $WantOut) -and ($WantFile -eq '' -or $after -ceq $WantFile)
        if ($ok) { Write-Host "  [ok]   $Name (exit $got)" }
        else {
            Write-Host "  [FAIL] $Name - expected exit $WantExit, got $got"
            Write-Host ($out.TrimEnd() -replace '(?m)^', '         ')
            $script:bad++
        }
    }

    $crlfGood = New-Backlog $good "`r`n"
    Run 'clean-passes'           $crlfGood                              @('-Quiet') 0 '' $crlfGood
    Run 'mismatch-fails-hints'   (New-Backlog $badTal "`r`n")           @()         1 'FixTally' ''
    # -FixTally: only the tally line changes (new numbers, today), CRLF kept, no BOM.
    Run 'fix-rewrites-crlf'      (New-Backlog $badTal "`r`n")           @('-FixTally') 0 '' (New-Backlog $fixedTl "`r`n")
    Run 'fix-rewrites-lf'        (New-Backlog $badTal "`n")             @('-FixTally') 0 '' (New-Backlog $fixedTl "`n")
    # Counts right but old date: -FixTally must not touch the file (no date bump).
    Run 'fix-noop-keeps-date'    $crlfGood                              @('-FixTally') 0 '' $crlfGood
    Run 'conflict-markers-fail'  (New-Backlog $good "`r`n" '<<<<<<< HEAD') @()      1 'conflict marker' ''
    Run 'conflict-equals-fail'   (New-Backlog $good "`n" '=======')     @()         1 'conflict marker' ''
    Run 'missing-tally-fails'    (New-Backlog 'no tally here' "`r`n")   @()         1 'tally line' ''

    Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
    $status = if ($script:bad -eq 0) { 'PASS' } else { 'FAIL' }
    Write-Host "check-backlog self-test: $status ($($script:n) cases, $($script:bad) failed)"
    exit $(if ($script:bad -eq 0) { 0 } else { 1 })
}

# Resolve the backlog relative to the repo root (this script lives in tests/scripts/).
if (-not $Path) {
    $repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
    $Path     = Join-Path $repoRoot 'backlog.md'
}

if (-not (Test-Path -LiteralPath $Path)) {
    Write-Host "check-backlog: SETUP ERROR - file not found: $Path"
    exit 2
}

try   { $lines = Get-Content -LiteralPath $Path -Encoding UTF8 }
catch { Write-Host "check-backlog: SETUP ERROR - cannot read $Path : $_"; exit 2 }

# Section heading -> the Status cell every row in it must carry.
$expectedStatus = [ordered]@{
    'Open'    = '🔴 Open'
    'On Hold' = '🟡 On Hold'
    'Closed'  = '✅ Closed'
}
$sectionOrder = @('Open', 'On Hold', 'Closed')

$violations = New-Object System.Collections.Generic.List[string]
function Add-Violation([int]$LineNo, [string]$Message) {
    $violations.Add(("  [FAIL] {0}:{1} - {2}" -f (Split-Path -Leaf $Path), $LineNo, $Message))
}

# ── Pass 1: walk the file, tracking section context and row facts ────────────────
$seenSections   = New-Object System.Collections.Generic.List[string]
$sectionLine    = @{}
$sectionHasHdr  = @{}
$sectionRowCount= @{}
$seenNumbers    = @{}

$currentSection = $null
$prevNumber     = $null
$awaitingDelim  = $false      # set on a header row: the very next line must be a delimiter
$headerLineNo   = 0

for ($i = 0; $i -lt $lines.Count; $i++) {
    $lineNo = $i + 1
    $line   = $lines[$i]

    # Section heading, e.g. "## ✅ Closed" — match on the trailing word(s), not the emoji.
    if ($line -match '^##\s+\S+\s+(.+?)\s*$') {
        $name = $Matches[1].Trim()
        if ($expectedStatus.Contains($name)) {
            if ($seenSections -contains $name) {
                Add-Violation $lineNo "duplicate section heading '$name'"
            } else {
                $seenSections.Add($name)
                $sectionLine[$name]     = $lineNo
                $sectionHasHdr[$name]   = $false
                $sectionRowCount[$name] = 0
            }
            $currentSection = $name
            $prevNumber     = $null      # ordering restarts per section
            $awaitingDelim  = $false
        }
        continue
    }

    # Table header row.
    if ($line -match '^\|\s*#\s*\|\s*Description\s*\|') {
        if ($currentSection) { $sectionHasHdr[$currentSection] = $true }
        $awaitingDelim = $true
        $headerLineNo  = $lineNo
        continue
    }

    # Delimiter row: legal only directly after a header.
    if ($line -match '^\|\s*-{2,}') {
        if (-not $awaitingDelim) {
            Add-Violation $lineNo "orphan delimiter row - not directly after a table header (splits the table above it)"
        }
        $awaitingDelim = $false
        continue
    }

    # Issue row.
    if ($line -match '^\|\s*(\d+)\s*\|') {
        $number = [int]$Matches[1]

        if ($awaitingDelim) {
            Add-Violation $headerLineNo "table header has no delimiter row beneath it - GitHub will not render this section as a table"
            $awaitingDelim = $false
        }

        if (-not $currentSection) {
            Add-Violation $lineNo "issue #$number sits outside any Open / On Hold / Closed section"
            continue
        }

        $sectionRowCount[$currentSection]++

        # 3. Descending order within the section.
        if ($currentSection -ne 'Open' -and $null -ne $prevNumber -and $number -ge $prevNumber) {
            Add-Violation $lineNo "#$number follows #$prevNumber - not descending within '$currentSection'"
        }
        $prevNumber = $number

        # 4. Duplicates across the whole file.
        if ($seenNumbers.ContainsKey($number)) {
            Add-Violation $lineNo "#$number already used at line $($seenNumbers[$number])"
        } else {
            $seenNumbers[$number] = $lineNo
        }

        # 6. Column count. Split on unescaped pipes only, so "\|" in a description is fine.
        #    A well-formed row is "| a | b | c | d |" -> 6 fragments (empty ends included).
        $unescaped = [regex]::Matches($line, '(?<!\\)\|').Count
        if ($unescaped -ne 5) {
            $cells = $unescaped - 1
            Add-Violation $lineNo "#$number has $cells cells, expected 4 - an unescaped '|' in the description (use '\|')"
        } else {
            # 5. Status cell (3rd of 4) matches the section. Only meaningful once the row
            #    splits cleanly, otherwise the cell indices are already wrong.
            $cells  = $line -split '(?<!\\)\|'
            $status = $cells[3].Trim()
            $want   = $expectedStatus[$currentSection]
            if ($status -ne $want) {
                Add-Violation $lineNo "#$number status is '$status', expected '$want'"
            }
        }
        continue
    }
}

# A header at the very end of the file with nothing after it.
if ($awaitingDelim) {
    Add-Violation $headerLineNo "table header has no delimiter row beneath it - GitHub will not render this section as a table"
}

# ── Pass 2: whole-file structure ─────────────────────────────────────────────────
foreach ($name in $sectionOrder) {
    if ($seenSections -notcontains $name) {
        Add-Violation 0 "missing required section '$name'"
    } elseif (-not $sectionHasHdr[$name]) {
        Add-Violation $sectionLine[$name] "section '$name' has no table header row"
    }
}

# Sections must appear in the canonical order (only judge the ones actually present).
$present = $sectionOrder | Where-Object { $seenSections -contains $_ }
$actual  = $seenSections | Where-Object { $sectionOrder -contains $_ }
if (($present -join ' > ') -ne ($actual -join ' > ')) {
    Add-Violation 0 "sections are ordered '$($actual -join ' > ')', expected '$($present -join ' > ')'"
}

# ── Pass 3: tally line and conflict markers ──────────────────────────────────────
# 8. Conflict markers anywhere in the file.
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match '^(<{7}|={7}|>{7})(\s|$)') {
        Add-Violation ($i + 1) "git conflict marker '$($Matches[1])' - resolve the merge, then run with -FixTally to regenerate the tally"
    }
}

# 7. The tally line must equal the real counts. -FixTally rewrites just that line (raw bytes
#    otherwise untouched, so line endings and the absence of a BOM are preserved).
$real = @{}
foreach ($s in $sectionOrder) { $real[$s] = [int]$(if ($sectionRowCount.ContainsKey($s)) { $sectionRowCount[$s] } else { 0 }) }
$realTotal = $real['Open'] + $real['On Hold'] + $real['Closed']
$dotChar   = [string][char]0x00B7

$raw = [IO.File]::ReadAllBytes($Path)
$enc = New-Object System.Text.UTF8Encoding($false)
$txt = $enc.GetString($raw)           # a BOM, if the file had one, survives as U+FEFF
$tm  = [regex]::Match($txt, "(?m)^$tallyRegex(?=\r?$)")

if (-not $tm.Success) {
    Add-Violation 0 "no tally line found under '# Issues' (expected '**Tally (updated YYYY-MM-DD): Open N $dotChar On Hold N $dotChar Closed N $dotChar Total N**')"
} else {
    $have = @([int]$tm.Groups[2].Value, [int]$tm.Groups[3].Value, [int]$tm.Groups[4].Value, [int]$tm.Groups[5].Value)
    $want = @($real['Open'], $real['On Hold'], $real['Closed'], $realTotal)
    if (($have -join ',') -ne ($want -join ',')) {
        # Never regenerate from a file whose sections did not parse: the counts would be zeros.
        if ($FixTally -and $seenSections.Count -lt 3) {
            Add-Violation 0 "-FixTally refused: not all of Open / On Hold / Closed were found, so the counts cannot be trusted"
        } elseif ($FixTally) {
            $today   = Get-Date -Format 'yyyy-MM-dd'
            $newLine = "**Tally (updated $today): Open $($real['Open']) $dotChar On Hold $($real['On Hold']) $dotChar Closed $($real['Closed']) $dotChar Total $realTotal**"
            $txt     = $txt.Substring(0, $tm.Index) + $newLine + $txt.Substring($tm.Index + $tm.Length)
            [IO.File]::WriteAllBytes($Path, $enc.GetBytes($txt))
            if (-not $Quiet) { Write-Host "  [fixed] tally line rewritten: $newLine" }
        } else {
            $lineNo = ($txt.Substring(0, $tm.Index) -split "`n").Count
            Add-Violation $lineNo ("tally says Open {0} / On Hold {1} / Closed {2} / Total {3} but the rows count Open {4} / On Hold {5} / Closed {6} / Total {7} - run: pwsh tests/scripts/check-backlog.ps1 -FixTally" -f `
                $have[0], $have[1], $have[2], $have[3], $want[0], $want[1], $want[2], $want[3])
        }
    }
}

# ── Report ───────────────────────────────────────────────────────────────────────
if (-not $Quiet) {
    foreach ($v in $violations) { Write-Host $v }
}

$totalRows = ($sectionRowCount.Values | Measure-Object -Sum).Sum
$counts    = ($sectionOrder | Where-Object { $sectionRowCount.ContainsKey($_) } |
              ForEach-Object { "$_ $($sectionRowCount[$_])" }) -join ', '

Write-Host ("check-backlog: {0} ({1} rows: {2}; {3} violation(s))" -f `
    $(if ($violations.Count -eq 0) { 'PASS' } else { 'FAIL' }), $totalRows, $counts, $violations.Count)

exit $(if ($violations.Count -eq 0) { 0 } else { 1 })
