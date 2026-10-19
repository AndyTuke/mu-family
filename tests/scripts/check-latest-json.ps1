<#
.SYNOPSIS
  Checks site/latest.json before it is pushed: schema, slugs, builds not above build_number.txt, trusted URLs.
#>
param([string]$Root = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
$ErrorActionPreference = 'Stop'

$file = Join-Path $Root 'site/latest.json'
if (-not (Test-Path $file)) { Write-Host 'latest.json not present yet: nothing to check'; exit 0 }

$errors = @()
$maxBuild = [int](Get-Content (Join-Path $Root 'build_number.txt') -Raw).Trim()
$slugs = 'mu-clid', 'mu-clid-midi', 'mu-tant', 'mu-toni', 'mu-on', 'mu-link'
$trusted = 'https://transwarp.me/'

try { $j = Get-Content $file -Raw | ConvertFrom-Json } catch { Write-Host "FAIL: not valid JSON"; exit 1 }

if ($j.schema -ne 1) { $errors += "schema must be 1 (found $($j.schema))" }
if ([int]$j.build -gt $maxBuild) { $errors += "build $($j.build) is above build_number.txt ($maxBuild)" }
foreach ($p in $j.products.PSObject.Properties) {
    if ($slugs -notcontains $p.Name) { $errors += "unknown product slug '$($p.Name)'" }
    $e = $p.Value
    if ([int]$e.build -le 0) { $errors += "$($p.Name): build must be a positive number" }
    if ([int]$e.build -gt $maxBuild) { $errors += "$($p.Name): build $($e.build) is above build_number.txt ($maxBuild)" }
    foreach ($k in 'download', 'notes', 'windows', 'linux') {
        $v = $e.$k
        if ($v -and -not ([string]$v).StartsWith($trusted) -and -not ([string]$v).StartsWith('https://github.com/AndyTuke/mu-family/')) {
            $errors += "$($p.Name).$k is not on transwarp.me or the project's GitHub: $v"
        }
    }
}

if ($errors.Count -gt 0) { $errors | ForEach-Object { Write-Host "FAIL: $_" }; exit 1 }
Write-Host "latest.json OK ($(@($j.products.PSObject.Properties).Count) products, build $($j.build))"
