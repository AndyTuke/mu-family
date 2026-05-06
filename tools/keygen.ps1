#Requires -Version 7.0
# keygen.ps1 — Run ONCE to generate the mu-Clid Ed25519 signing key pair.
#
# Outputs:
#   tools\private.pem  — Private key (PEM). KEEP SECRET. Back up to password manager.
#                        Never commit — listed in .gitignore.
#   tools\public.pem   — Public key (PEM). Safe to commit.
#   tools\public.key   — Public key (DER binary, 44 bytes). Same content, different format.
#
# After running, paste the printed C++ array into Source/License/LicenseChecker.h
# to replace the placeholder kPublicKey bytes, then rebuild the plugin.
#
# Uses: OpenSSL from Git for Windows (C:\Program Files\Git\mingw64\bin\openssl.exe)
#
# WARNING: If you regenerate keys, ALL existing license files become invalid.
#          Every customer must be re-issued a new license. Treat the private key
#          like a master password — store it in your password manager as a backup.

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$openssl    = "C:\Program Files\Git\mingw64\bin\openssl.exe"
$privatePem = "$PSScriptRoot\private.pem"
$publicPem  = "$PSScriptRoot\public.pem"
$publicDer  = "$PSScriptRoot\public.key"

if (-not (Test-Path $openssl))
{
    Write-Error "OpenSSL not found at '$openssl'. Install Git for Windows and retry."
    exit 1
}

if (Test-Path $privatePem)
{
    Write-Warning "private.pem already exists at $privatePem"
    $answer = Read-Host "Overwrite? This will invalidate ALL existing licenses. Type YES to confirm"
    if ($answer -ne "YES") { Write-Host "Aborted."; exit 0 }
}

# Generate Ed25519 private key
& $openssl genpkey -algorithm Ed25519 -out $privatePem
if ($LASTEXITCODE -ne 0) { Write-Error "Key generation failed."; exit 1 }

# Derive public key (PEM and DER)
& $openssl pkey -in $privatePem -pubout -out $publicPem
& $openssl pkey -in $privatePem -pubout -outform DER -out $publicDer
if ($LASTEXITCODE -ne 0) { Write-Error "Public key export failed."; exit 1 }

# Extract the raw 32-byte public key from the 44-byte SPKI DER envelope.
# Structure: 12 bytes of header + 32 bytes of key.
$pubDerBytes = [System.IO.File]::ReadAllBytes($publicDer)
$pubRaw      = $pubDerBytes[12..43]

# Format as C++ array initialiser
$hex      = $pubRaw | ForEach-Object { "0x{0:x2}" -f $_ }
$cppArray = ($hex[0..15] -join ", ") + ",`n        " + ($hex[16..31] -join ", ")

Write-Host ""
Write-Host "Keys generated successfully."
Write-Host ""
Write-Host "Paste this into Source/License/LicenseChecker.h (replace the placeholder array):"
Write-Host ""
Write-Host "    static constexpr uint8_t kPublicKey[32] = {"
Write-Host "        $cppArray"
Write-Host "    };"
Write-Host ""
Write-Host "Private key : $privatePem  <-- KEEP SECRET, back up now"
Write-Host "Public key  : $publicPem"
