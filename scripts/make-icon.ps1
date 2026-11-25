# Draws the Disk Pro icon (original artwork) and writes assets/icon.png + assets/icon.ico.
Add-Type -AssemblyName System.Drawing
$root = Split-Path -Parent $PSScriptRoot
$assets = Join-Path $root "assets"
New-Item -ItemType Directory -Force $assets | Out-Null

function New-IconBitmap([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.Clear([System.Drawing.Color]::Transparent)
    $r = [int]($size * 0.22)
    $rect = New-Object System.Drawing.Rectangle 0, 0, ($size - 1), ($size - 1)
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $r * 2
    $path.AddArc($rect.X, $rect.Y, $d, $d, 180, 90)
    $path.AddArc($rect.Right - $d, $rect.Y, $d, $d, 270, 90)
    $path.AddArc($rect.Right - $d, $rect.Bottom - $d, $d, $d, 0, 90)
    $path.AddArc($rect.X, $rect.Bottom - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush $rect, ([System.Drawing.Color]::FromArgb(255, 20, 170, 150)), ([System.Drawing.Color]::FromArgb(255, 30, 90, 200)), 55
    $g.FillPath($brush, $path)
    # disk body with a partition bar
    $white = [System.Drawing.Color]::FromArgb(255, 255, 255, 255)
    $pen = New-Object System.Drawing.Pen $white, ([Math]::Max(1.0, $size * 0.05))
    $x = $size * 0.16; $w = $size * 0.68; $y = $size * 0.30; $h = $size * 0.40
    $rr = $size * 0.07
    $dp = New-Object System.Drawing.Drawing2D.GraphicsPath
    $dd = $rr * 2
    $dp.AddArc($x, $y, $dd, $dd, 180, 90); $dp.AddArc($x + $w - $dd, $y, $dd, $dd, 270, 90)
    $dp.AddArc($x + $w - $dd, $y + $h - $dd, $dd, $dd, 0, 90); $dp.AddArc($x, $y + $h - $dd, $dd, $dd, 90, 90)
    $dp.CloseFigure()
    $g.DrawPath($pen, $dp)
    $fill = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 255, 255, 255))
    $bx = $x + $size * 0.07; $by = $y + $h * 0.40; $bh = $h * 0.34
    $inner = $w - $size * 0.14
    $g.FillRectangle($fill, [single]$bx, [single]$by, [single]($inner * 0.42), [single]$bh)
    $g.FillRectangle($fill, [single]($bx + $inner * 0.47), [single]$by, [single]($inner * 0.30), [single]$bh)
    $g.FillRectangle($fill, [single]($bx + $inner * 0.82), [single]$by, [single]($inner * 0.18), [single]$bh)
    $g.Dispose()
    return $bmp
}

$sizes = 16, 24, 32, 48, 64, 128, 256
$pngs = @()
foreach ($s in $sizes) {
    $bmp = New-IconBitmap $s
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $pngs += , @($s, $ms.ToArray())
    if ($s -eq 256) { $bmp.Save((Join-Path $assets "icon.png"), [System.Drawing.Imaging.ImageFormat]::Png) }
    $bmp.Dispose()
}
$out = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $out
$bw.Write([UInt16]0); $bw.Write([UInt16]1); $bw.Write([UInt16]$pngs.Count)
$offset = 6 + 16 * $pngs.Count
foreach ($p in $pngs) {
    $s = $p[0]; $data = $p[1]
    $dim = if ($s -ge 256) { 0 } else { $s }
    $bw.Write([byte]$dim); $bw.Write([byte]$dim); $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([UInt16]1); $bw.Write([UInt16]32)
    $bw.Write([UInt32]$data.Length); $bw.Write([UInt32]$offset)
    $offset += $data.Length
}
foreach ($p in $pngs) { $bw.Write([byte[]]$p[1]) }
[System.IO.File]::WriteAllBytes((Join-Path $assets "icon.ico"), $out.ToArray())
Write-Host "icon written"
