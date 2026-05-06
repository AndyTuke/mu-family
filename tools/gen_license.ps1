#Requires -Version 7.0
# gen_license.ps1 — Generate a muclid.lic license file for one customer.
#
# Usage (Phase 1 — manual):
#   .\gen_license.ps1 -Name "John Smith" -Email "john@example.com" -Order "ORD-12345"
#
# Optional parameters:
#   -Expires        "lifetime" (default) or an expiry date "2027-12-31"
#   -PrivateKeyPath Path to private.pem (defaults to tools\private.pem)
#   -OutputPath     Where to write the .lic file (defaults to .\muclid.lic)
#
# Phase 2 (web service): port this logic to your server. The canonical payload
# format and Ed25519 signing algorithm are the same — just call OpenSSL or any
# Ed25519 library with the same private key.
#
# Uses: OpenSSL from Git for Windows (C:\Program Files\Git\mingw64\bin\openssl.exe)

[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $Name,
    [Parameter(Mandatory)] [string] $Email,
    [Parameter(Mandatory)] [string] $Order,
    [string] $Expires        = "lifetime",
    [string] $PrivateKeyPath = "$PSScriptRoot\private.pem",
    [string] $OutputPath     = "muclid.lic"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$openssl = "C:\Program Files\Git\mingw64\bin\openssl.exe"

if (-not (Test-Path $openssl))    { Write-Error "OpenSSL not found at '$openssl'."; exit 1 }
if (-not (Test-Path $PrivateKeyPath)) { Write-Error "Private key not found at '$PrivateKeyPath'. Run keygen.ps1 first."; exit 1 }

# ── Build canonical payload ─────────────────────────────────────────────────
# Fields sorted alphabetically. The plugin rebuilds this exact string and
# verifies the signature — any change here invalidates all existing licenses.
$issued    = (Get-Date -Format "yyyy-MM-dd")
$canonical = "email=$Email`nexpires=$Expires`nissued=$issued`nname=$Name`norder=$Order`nproduct=mu-Clid"

# ── Sign with Ed25519 ───────────────────────────────────────────────────────
$tmpMsg = [System.IO.Path]::GetTempFileName()
$tmpSig = [System.IO.Path]::GetTempFileName()
try
{
    # Write payload as raw UTF-8 (no BOM)
    [System.IO.File]::WriteAllBytes($tmpMsg, [System.Text.Encoding]::UTF8.GetBytes($canonical))

    & $openssl pkeyutl -sign -inkey $PrivateKeyPath -out $tmpSig -rawin -in $tmpMsg
    if ($LASTEXITCODE -ne 0) { Write-Error "Signing failed."; exit 1 }

    $sigBytes = [System.IO.File]::ReadAllBytes($tmpSig)
    if ($sigBytes.Length -ne 64) { Write-Error "Unexpected signature length $($sigBytes.Length)."; exit 1 }
}
finally
{
    Remove-Item $tmpMsg, $tmpSig -ErrorAction SilentlyContinue
}

$sigBase64 = [Convert]::ToBase64String($sigBytes)

# ── Write license file ──────────────────────────────────────────────────────
# Fields alphabetical — predictable and diffable.
$license = [ordered]@{
    email     = $Email
    expires   = $Expires
    issued    = $issued
    name      = $Name
    order     = $Order
    product   = "mu-Clid"
    signature = $sigBase64
}

$json = $license | ConvertTo-Json -Depth 1
Set-Content -Path $OutputPath -Value $json -Encoding UTF8NoBOM

Write-Host "License written: $OutputPath"
Write-Host "  Name   : $Name"
Write-Host "  Email  : $Email"
Write-Host "  Order  : $Order"
Write-Host "  Issued : $issued"
Write-Host "  Expires: $Expires"
