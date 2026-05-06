#Requires -Version 7.0
# verify_license.ps1 — Test-verify a muclid.lic file against the public key.
# Use this before sending a license to a customer, or when helping a customer
# who reports their license is not being accepted.
#
# Usage:
#   .\verify_license.ps1 -LicensePath "muclid.lic"
#   .\verify_license.ps1 -LicensePath "muclid.lic" -PublicKeyPath ".\public.pem"
#
# Uses: OpenSSL from Git for Windows (C:\Program Files\Git\mingw64\bin\openssl.exe)

[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $LicensePath,
    [string] $PublicKeyPath = "$PSScriptRoot\public.pem"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$openssl = "C:\Program Files\Git\mingw64\bin\openssl.exe"

if (-not (Test-Path $openssl))     { Write-Error "OpenSSL not found at '$openssl'."; exit 1 }
if (-not (Test-Path $PublicKeyPath)) { Write-Error "Public key not found at '$PublicKeyPath'."; exit 1 }
if (-not (Test-Path $LicensePath))   { Write-Error "License file not found: '$LicensePath'"; exit 1 }

# ── Parse license JSON ───────────────────────────────────────────────────────
$json    = Get-Content -Path $LicensePath -Raw -Encoding UTF8
$license = $json | ConvertFrom-Json

foreach ($field in @("email","expires","issued","name","order","product","signature"))
{
    if (-not $license.PSObject.Properties[$field])
    {
        Write-Host "INVALID — missing field: $field" -ForegroundColor Red; exit 1
    }
}

# ── Rebuild canonical payload ────────────────────────────────────────────────
$canonical = "email=$($license.email)`nexpires=$($license.expires)`nissued=$($license.issued)`nname=$($license.name)`norder=$($license.order)`nproduct=$($license.product)"

# ── Verify signature ─────────────────────────────────────────────────────────
$sigBytes = [Convert]::FromBase64String($license.signature)
if ($sigBytes.Length -ne 64) { Write-Host "INVALID — signature is $($sigBytes.Length) bytes, expected 64." -ForegroundColor Red; exit 1 }

$tmpMsg = [System.IO.Path]::GetTempFileName()
$tmpSig = [System.IO.Path]::GetTempFileName()
try
{
    [System.IO.File]::WriteAllBytes($tmpMsg, [System.Text.Encoding]::UTF8.GetBytes($canonical))
    [System.IO.File]::WriteAllBytes($tmpSig, $sigBytes)

    & $openssl pkeyutl -verify -pubin -inkey $PublicKeyPath -sigfile $tmpSig -rawin -in $tmpMsg 2>&1 | Out-Null
    $valid = ($LASTEXITCODE -eq 0)
}
finally
{
    Remove-Item $tmpMsg, $tmpSig -ErrorAction SilentlyContinue
}

if ($valid)
{
    Write-Host "VALID license" -ForegroundColor Green
    Write-Host "  Name   : $($license.name)"
    Write-Host "  Email  : $($license.email)"
    Write-Host "  Order  : $($license.order)"
    Write-Host "  Issued : $($license.issued)"
    Write-Host "  Expires: $($license.expires)"

    # Check expiry locally too
    if ($license.expires -ne "lifetime")
    {
        $today  = Get-Date -Format "yyyy-MM-dd"
        if ($license.expires -lt $today)
        {
            Write-Host "  ** EXPIRED on $($license.expires) **" -ForegroundColor Yellow
        }
    }
}
else
{
    Write-Host "INVALID — signature verification failed." -ForegroundColor Red
    exit 1
}
