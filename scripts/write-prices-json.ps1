<#
.SYNOPSIS
  Writes site/prices.json from Lemon Squeezy, so the website shows the store's prices and checkout links (one source of truth).
.DESCRIPTION
  Run by /release before the batched site push, and any time a price changes in Lemon Squeezy. Reads every published product,
  takes its published fixed-price variant, and writes the price (GBP) and the checkout link per product slug. The API key is read
  from the secrets file outside the repo and never written anywhere. A static file: the site costs nothing extra to serve it.
  Schema: { currency, symbol, generated, products: { "<slug>": { price, priceText, checkout } } }.
#>
param(
    [string]$Root    = (Split-Path -Parent $PSScriptRoot),
    [string]$EnvFile = 'D:\OneDrive - Andy Tuke\Development\secrets\transwarp.env'
)
$ErrorActionPreference = 'Stop'

$line = (Get-Content $EnvFile -Encoding UTF8 | Where-Object { $_ -match '^\s*LEMONSQUEEZY_API_KEY\s*=' } | Select-Object -First 1)
if (-not $line) { throw "LEMONSQUEEZY_API_KEY not found in $EnvFile" }
$key = ($line -replace '^\s*LEMONSQUEEZY_API_KEY\s*=\s*', '').Trim().Trim('"')
$headers = @{ Authorization = "Bearer $key"; Accept = 'application/vnd.api+json' }
function Get-Ls([string]$path) { Invoke-RestMethod -Uri "https://api.lemonsqueezy.com/v1/$path" -Headers $headers }

# Product name -> site slug (the store names read "μ-Clid: Euclidean rhythm sequencer", so match on the word after the mu).
$slugs = [ordered]@{ 'Clid' = 'mu-clid'; 'Tant' = 'mu-tant'; 'Toni' = 'mu-toni'; 'On' = 'mu-on'; 'Tilate' = 'mu-tilate'; 'Tronic' = 'mu-tronic' }

$products = (Get-Ls 'products?page[size]=50').data
$variants = (Get-Ls 'variants?page[size]=100').data
$out = [ordered]@{}
foreach ($p in $products) {
    if ($p.attributes.status -ne 'published') { continue }
    $slug = $null
    foreach ($k in $slugs.Keys) { if ($p.attributes.name -match "^\S?-?$k\b" -or $p.attributes.name -match "-$k[: ]") { $slug = $slugs[$k]; break } }
    if (-not $slug) { Write-Warning "Skipping '$($p.attributes.name)': no site slug"; continue }
    $v = $variants | Where-Object { "$($_.attributes.product_id)" -eq "$($p.id)" -and $_.attributes.status -eq 'published' } | Select-Object -First 1
    if (-not $v) { Write-Warning "No published variant for $slug"; continue }
    $pounds = [math]::Round($v.attributes.price / 100, 2)
    $text = if ($pounds -eq [math]::Floor($pounds)) { "£{0}" -f [int]$pounds } else { "£{0:0.00}" -f $pounds }
    $out[$slug] = [ordered]@{ price = $pounds; priceText = $text; checkout = $p.attributes.buy_now_url }
}

$doc = [ordered]@{ currency = 'GBP'; symbol = '£'; generated = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'); products = $out }
$path = Join-Path $Root 'site/prices.json'
$json = $doc | ConvertTo-Json -Depth 5
[System.IO.File]::WriteAllText($path, $json + "`n", (New-Object System.Text.UTF8Encoding($false)))
Write-Host "Wrote $path"
$out.GetEnumerator() | ForEach-Object { Write-Host ("  {0,-10} {1,-6} {2}" -f $_.Key, $_.Value.priceText, $_.Value.checkout) }
