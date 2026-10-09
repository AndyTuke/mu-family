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
#
#   pwsh tests/scripts/check-backlog.ps1                  # check backlog.md at the repo root
#   pwsh tests/scripts/check-backlog.ps1 -Path other.md   # check a different file
#   pwsh tests/scripts/check-backlog.ps1 -Quiet           # summary line only
#
# Exit code 0 = clean, 1 = one or more violations, 2 = setup error (file missing/unreadable).

param(
    [string]$Path,
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'

# The status cells are emoji. Force UTF-8 out so they survive redirection into a CI log or
# a pipe through a non-UTF-8 shell, where they would otherwise arrive as "?".
try { [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false) } catch { }

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
