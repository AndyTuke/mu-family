# Weekly download stats for the Windows zips on GitHub Releases (free: reads the public Releases API through gh).
# Appends one cumulative row per product to downloads.csv and rewrites downloads-report.md (latest totals + change since the previous run).
# Output folder defaults to the OneDrive stats folder so the report syncs to Andy's phone.
param(
    [string] $OutDir = 'D:\OneDrive - Andy Tuke\Development\stats',
    [string] $Repo   = 'AndyTuke/mu-family'
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $OutDir | Out-Null
$csv = Join-Path $OutDir 'downloads.csv'
$md  = Join-Path $OutDir 'downloads-report.md'

# Phase 1: every release asset -> Windows zips only, product name from the file name (version suffix dropped; Lite = the old MIDI name).
$rows = gh api --paginate "repos/$Repo/releases" --jq '.[] | .assets[] | [.name, .download_count] | @tsv'
if ($LASTEXITCODE -ne 0) { throw 'gh api failed (is gh logged in?)' }
$totals = @{}
foreach ($line in $rows) {
    $name, $count = $line -split "`t"
    if ($name -notmatch 'Windows\.zip$') { continue }
    $product = ($name -replace '-Windows\.zip$', '') -replace '-v?\d.*$', ''
    if ($product -eq 'mu-Clid-Lite') { $product = 'mu-Clid-MIDI' }
    $totals[$product] = [int]$totals[$product] + [int]$count
}

# Phase 2: append today's cumulative totals to the history.
$today = (Get-Date).ToString('yyyy-MM-dd')
if (-not (Test-Path $csv)) { 'date,product,downloads' | Set-Content $csv -Encoding utf8 }
$history = @(Import-Csv $csv)
foreach ($p in $totals.Keys) { Add-Content $csv "$today,$p,$($totals[$p])" -Encoding utf8 }

# Phase 3: the report - latest totals, and the change since the previous run's date.
$prevDate = ($history | Where-Object { $_.date -lt $today } | Sort-Object date | Select-Object -Last 1).date
$prev = @{}
if ($prevDate) { $history | Where-Object { $_.date -eq $prevDate } | ForEach-Object { $prev[$_.product] = [int]$_.downloads } }
$lines = @("# mu downloads (Windows zips, all versions)", "", ("Run $today" + $(if ($prevDate) { "; change since $prevDate" } else { '; first run, no change column yet' })), "",
           "| Product | Total | Change |", "|---|---:|---:|")
$sum = 0
foreach ($p in ($totals.Keys | Sort-Object { -$totals[$_] })) {
    $delta = if ($prevDate) { '+' + ($totals[$p] - [int]$prev[$p]) } else { '-' }
    $lines += "| $p | $($totals[$p]) | $delta |"
    $sum += $totals[$p]
}
$lines += "| **All** | **$sum** | |"
$lines += ""; $lines += "Counts include repeat downloads, bots and scanners."
$lines | Set-Content $md -Encoding utf8
$lines
