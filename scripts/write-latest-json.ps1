<#
.SYNOPSIS
  Writes site/latest.json, the file every app reads to learn whether a newer build exists.
.DESCRIPTION
  Run by /release after CI has finished and the GitHub release assets exist, before the batched site push.
  -Products lists the slugs actually released in this build; a product left out keeps its previous entry,
  so a product that did not ship does not claim the new build. Schema: docs/design-plugin-family.md, "Update check".
#>
param(
    [string[]]$Products = @('mu-clid', 'mu-clid-midi', 'mu-tant', 'mu-toni', 'mu-on', 'mu-link', 'mu-tronic'),
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)
$ErrorActionPreference = 'Stop'
$Products = @($Products | ForEach-Object { $_ -split ',' })   # -File passes a comma list as one string

$build   = [int](Get-Content (Join-Path $Root 'build_number.txt') -Raw).Trim()
$live    = (Get-Content (Join-Path $Root 'version.txt') -Raw).Trim()
$pre     = (Get-Content (Join-Path $Root 'version-pre.txt') -Raw).Trim()
$preSlugs = @('mu-toni', 'mu-on')   # pre-release products use version-pre.txt
$outFile = Join-Path $Root 'site/latest.json'

$existing = @{}
if (Test-Path $outFile) {
    $old = Get-Content $outFile -Raw | ConvertFrom-Json
    foreach ($p in $old.products.PSObject.Properties) { $existing[$p.Name] = $p.Value }
}

$entries = [ordered]@{}
foreach ($slug in 'mu-clid', 'mu-clid-midi', 'mu-tant', 'mu-toni', 'mu-on', 'mu-link', 'mu-tronic') {
    if ($Products -contains $slug) {
        $base = if ($preSlugs -contains $slug) { $pre } else { $live }
        $entries[$slug] = [ordered]@{
            version  = "v$base.$build"
            build    = $build
            download = 'https://transwarp.me/download'
        }
    } elseif ($existing.ContainsKey($slug)) {
        $entries[$slug] = $existing[$slug]
    }
}

$doc = [ordered]@{
    schema   = 1
    build    = $build
    released = (Get-Date -Format 'yyyy-MM-dd')
    products = $entries
}
$doc | ConvertTo-Json -Depth 5 | Set-Content -Path $outFile -Encoding utf8
Write-Host "Wrote $outFile (build $build, products: $($Products -join ', '))"
