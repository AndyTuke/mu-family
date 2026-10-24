# capture-ui.ps1 - drive a mu-family Standalone window and save screenshots (manuals, website).
#
# Dot-source it:   . tests\scripts\capture-ui.ps1
# Then:
#   $app = Start-MuApp -Exe 'C:\path\to\COPY\mu-On.exe'        # always a COPY of the exe, so a build can replace the original
#   Focus-MuApp $app
#   Click-Mu $app 1100 45                                       # window-relative pixels (the window as PrintWindow draws it)
#   Save-MuWindow $app 'D:\...\main-window.png'                 # the whole window
#   Save-MuWindow $app 'D:\...\mixer.png' -Crop 100,80,900,500  # a region: x,y,w,h
#   Save-MuScreen  $app 'D:\...\menu.png' -Crop ...             # same, from the real screen, so a native popup menu shows
#   Stop-MuApp $app
# Keys: Key-Mu 0x28 (Down), 0x0D (Enter), 0x1B (Esc). Type-Mu 'text'. Drag-Mu $app x1 y1 x2 y2 [-Alt] [-Shift].
# Needs an unlocked interactive desktop: it moves the mouse. Do not run while someone is using the machine.

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public class MuWin {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int hgt, bool repaint);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
  public struct RECT { public int L, T, R, B; }
}
'@
[void][MuWin]::SetProcessDPIAware()

function Start-MuApp([string]$Exe, [int]$WaitSec = 10) {
    $p = Start-Process -FilePath $Exe -PassThru
    Start-Sleep -Seconds $WaitSec
    $p.Refresh()
    $p | Add-Member -NotePropertyName Hwnd -NotePropertyValue $p.MainWindowHandle -PassThru
}
function Stop-MuApp($app) { if (-not $app.HasExited) { Stop-Process -Id $app.Id -Force } }
function Get-MuRect($app) { $r = New-Object MuWin+RECT; [void][MuWin]::GetWindowRect($app.Hwnd, [ref]$r); $r }
function Focus-MuApp($app) { [void][MuWin]::SetForegroundWindow($app.Hwnd); Start-Sleep -Milliseconds 300 }
# Put the window at a fixed place and size (client sizes differ a little by title bar; the whole window rect is what is saved).
function Place-MuApp($app, [int]$x = 20, [int]$y = 20, [int]$w = 0, [int]$h = 0) {
    $r = Get-MuRect $app
    if ($w -le 0) { $w = $r.R - $r.L }; if ($h -le 0) { $h = $r.B - $r.T }
    [void][MuWin]::MoveWindow($app.Hwnd, $x, $y, $w, $h, $true); Start-Sleep -Milliseconds 600
}
function Move-MuMouse($app, [int]$x, [int]$y) { $r = Get-MuRect $app; [void][MuWin]::SetCursorPos($r.L + $x, $r.T + $y); Start-Sleep -Milliseconds 120 }
function Click-Mu($app, [int]$x, [int]$y, [switch]$Right, [switch]$Double, [int]$Settle = 500) {
    Move-MuMouse $app $x $y
    $down = if ($Right) { 8 } else { 2 }; $up = if ($Right) { 16 } else { 4 }
    $n = if ($Double) { 2 } else { 1 }
    for ($i = 0; $i -lt $n; $i++) {
        [MuWin]::mouse_event($down, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 60
        [MuWin]::mouse_event($up, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 80
    }
    Start-Sleep -Milliseconds $Settle
}
function Drag-Mu($app, [int]$x1, [int]$y1, [int]$x2, [int]$y2, [switch]$Alt, [switch]$Shift, [int]$Steps = 12) {
    Move-MuMouse $app $x1 $y1
    if ($Alt)   { [MuWin]::keybd_event(0x12, 0, 0, [IntPtr]::Zero) }
    if ($Shift) { [MuWin]::keybd_event(0x10, 0, 0, [IntPtr]::Zero) }
    [MuWin]::mouse_event(2, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 80
    $r = Get-MuRect $app
    for ($i = 1; $i -le $Steps; $i++) {
        [void][MuWin]::SetCursorPos($r.L + [int]($x1 + ($x2 - $x1) * $i / $Steps), $r.T + [int]($y1 + ($y2 - $y1) * $i / $Steps)); Start-Sleep -Milliseconds 25
    }
    [MuWin]::mouse_event(4, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 80
    if ($Alt)   { [MuWin]::keybd_event(0x12, 0, 2, [IntPtr]::Zero) }
    if ($Shift) { [MuWin]::keybd_event(0x10, 0, 2, [IntPtr]::Zero) }
    Start-Sleep -Milliseconds 400
}
function Key-Mu([byte]$Vk, [int]$Settle = 250) {
    [MuWin]::keybd_event($Vk, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 40; [MuWin]::keybd_event($Vk, 0, 2, [IntPtr]::Zero); Start-Sleep -Milliseconds $Settle
}
function Type-Mu([string]$Text) { foreach ($c in $Text.ToCharArray()) { $vk = [byte][char]::ToUpper($c); Key-Mu $vk 60 } }

function Crop-Bitmap($bmp, $Crop) {
    if (-not $Crop) { return $bmp }
    $rect = New-Object System.Drawing.Rectangle ([int]$Crop[0]), ([int]$Crop[1]), ([int]$Crop[2]), ([int]$Crop[3])
    $out = $bmp.Clone($rect, $bmp.PixelFormat); $bmp.Dispose(); $out
}
# The window's own drawing (works behind other windows; native popup menus are not in it).
function Save-MuWindow($app, [string]$Path, [int[]]$Crop = $null) {
    $r = Get-MuRect $app
    $bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
    $g = [System.Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc()
    [void][MuWin]::PrintWindow($app.Hwnd, $dc, 2); $g.ReleaseHdc($dc); $g.Dispose()
    $bmp = Crop-Bitmap $bmp $Crop
    New-Item -ItemType Directory -Force (Split-Path $Path) | Out-Null
    $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
}
# The real screen over the window (shows native popup menus / dropdowns; the window must be in front and unobscured).
function Save-MuScreen($app, [string]$Path, [int[]]$Crop = $null) {
    $r = Get-MuRect $app
    $bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size); $g.Dispose()
    $bmp = Crop-Bitmap $bmp $Crop
    New-Item -ItemType Directory -Force (Split-Path $Path) | Out-Null
    $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
}
