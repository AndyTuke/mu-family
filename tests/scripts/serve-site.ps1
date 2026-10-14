# Serves the site/ folder at http://localhost:8080 for local preview.
# Read-only: does not touch git or Netlify. Stop with Ctrl+C.
# Usage: pwsh tests/scripts/serve-site.ps1 [-Port 8080]
param([int]$Port = 8080)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..\site')).Path
$url = "http://localhost:$Port/"

Write-Host "Serving $root"
Write-Host "Open $url  (press Ctrl+C to stop)"

# Preferred: Python's built-in server (correct MIME types, no extra setup).
$py = Get-Command python -ErrorAction SilentlyContinue
if ($py) {
    try {
        & $py.Source -m http.server $Port --bind 127.0.0.1 --directory $root
        Write-Host "Preview server stopped."
        return
    } catch {
        Write-Host "Python server failed ($($_.Exception.Message)); using the PowerShell fallback."
    }
}

# Fallback: System.Net.HttpListener with an explicit MIME table.
$mime = @{
    '.html' = 'text/html; charset=utf-8'; '.htm' = 'text/html; charset=utf-8'
    '.css' = 'text/css; charset=utf-8'; '.js' = 'application/javascript; charset=utf-8'
    '.json' = 'application/json'; '.xml' = 'application/xml'; '.txt' = 'text/plain; charset=utf-8'
    '.png' = 'image/png'; '.jpg' = 'image/jpeg'; '.jpeg' = 'image/jpeg'; '.gif' = 'image/gif'
    '.svg' = 'image/svg+xml'; '.ico' = 'image/x-icon'; '.webp' = 'image/webp'
    '.woff' = 'font/woff'; '.woff2' = 'font/woff2'; '.pdf' = 'application/pdf'
}
$listener = [System.Net.HttpListener]::new()
$listener.Prefixes.Add($url)
$listener.Start()
try {
    while ($listener.IsListening) {
        $ctx = $listener.GetContextAsync()
        # Poll so Ctrl+C can interrupt the wait.
        while (-not $ctx.AsyncWaitHandle.WaitOne(200)) { }
        $c = $ctx.Result
        $rel = [Uri]::UnescapeDataString($c.Request.Url.AbsolutePath).TrimStart('/')
        if ($rel -eq '' -or $rel.EndsWith('/')) { $rel += 'index.html' }
        $file = [IO.Path]::GetFullPath((Join-Path $root $rel))
        if ($file.StartsWith($root) -and (Test-Path -LiteralPath $file -PathType Leaf)) {
            $ext = [IO.Path]::GetExtension($file).ToLower()
            $c.Response.ContentType = if ($mime.ContainsKey($ext)) { $mime[$ext] } else { 'application/octet-stream' }
            $bytes = [IO.File]::ReadAllBytes($file)
            $c.Response.StatusCode = 200
            $c.Response.OutputStream.Write($bytes, 0, $bytes.Length)
        } else {
            $c.Response.StatusCode = 404
        }
        $c.Response.Close()
    }
} finally {
    $listener.Stop()
    Write-Host "Preview server stopped."
}
