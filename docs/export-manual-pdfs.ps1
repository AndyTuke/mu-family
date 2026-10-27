# Exports every docs/*/*User Manual.docx to a web-named PDF (Word automation, PowerShell 7).
# Usage: pwsh docs/export-manual-pdfs.ps1 -OutDir <folder>
# The .docx files are never modified: each is copied to a temp folder and the copy is exported,
# in a private Word instance, so a manual Andy has open in Word is not touched.
param(
    [Parameter(Mandatory = $true)][string]$OutDir
)
$ErrorActionPreference = "Stop"

# Source .docx (by product folder) -> web-friendly PDF name.
$map = [ordered]@{
    'mu-clid'      = @{ Docx = 'mu-Clid User Manual.docx';      Pdf = 'mu-Clid-User-Manual.pdf' }
    'mu-tant'      = @{ Docx = 'mu-Tant User Manual.docx';      Pdf = 'mu-Tant-User-Manual.pdf' }
    'mu-toni'      = @{ Docx = 'mu-Toni User Manual.docx';      Pdf = 'mu-Toni-User-Manual.pdf' }
    'mu-on'        = @{ Docx = 'mu-On User Manual.docx';        Pdf = 'mu-On-User-Manual.pdf' }
    'mu-clid-midi' = @{ Docx = 'mu-Clid MIDI User Manual.docx'; Pdf = 'mu-Clid-MIDI-User-Manual.pdf' }
    'mu-link'      = @{ Docx = 'mu-link User Manual.docx';      Pdf = 'mu-Link-User-Manual.pdf' }
    'mu-tronic'    = @{ Docx = 'mu-Tronic User Manual.docx';    Pdf = 'mu-Tronic-User-Manual.pdf' }
}

New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$tmp = Join-Path ([IO.Path]::GetTempPath()) ("mu-manual-pdf-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $tmp | Out-Null

$word = New-Object -ComObject Word.Application
$word.Visible = $false
$word.DisplayAlerts = 0
try {
    foreach ($key in $map.Keys) {
        $src = Join-Path $PSScriptRoot "$key\$($map[$key].Docx)"
        if (-not (Test-Path -LiteralPath $src)) { Write-Warning "Missing: $src"; continue }
        $copy = Join-Path $tmp "$key.docx"
        Copy-Item -LiteralPath $src -Destination $copy
        $out = Join-Path $OutDir $map[$key].Pdf
        $doc = $word.Documents.Open($copy, $false, $true, $false)   # no conversion dialog, read-only, not in recent files
        try {
            # PDF (17), print quality, whole document, heading bookmarks, tagged structure.
            $doc.ExportAsFixedFormat($out, 17, $false, 0, 0, 1, 1, 0, $true, $true, 1, $true, $true, $false)
        }
        finally { $doc.Close($false) }
        $f = Get-Item -LiteralPath $out
        Write-Host ("{0}  {1:N2} MB" -f $f.Name, ($f.Length / 1MB))
    }
}
finally {
    $word.Quit($false)
    [void][Runtime.InteropServices.Marshal]::ReleaseComObject($word)
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
