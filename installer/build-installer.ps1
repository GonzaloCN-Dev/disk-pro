# Builds the Windows installer. Prerequisites: .\build.cmd already run, and Inno Setup 6 (winget install JRSoftware.InnoSetup).
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$payload = Join-Path $PSScriptRoot "payload"

$cmake = Get-Content (Join-Path $root "CMakeLists.txt") -Raw
if ($cmake -notmatch 'project\(DiskPro VERSION ([0-9.]+)') { throw "Cannot read the version from CMakeLists.txt" }
$version = $Matches[1]

$icon = Join-Path $root "assets\icon.ico"
if (-not (Test-Path $icon)) { & (Join-Path $root "scripts\make-icon.ps1") }

foreach ($f in "build\diskpro-gui.exe", "build\diskpro-cli.exe", "build\diskpro-inventory.exe", "build\diskpro-tools.exe", "build\diskpro-restore.exe", "build\diskpro-clone.exe", "build\diskpro-part.exe", "build\diskpro-vhd.exe", ".tools\slint\lib\slint_cpp.dll", ".tools\slint\licenses\LICENSE.md") {
    if (-not (Test-Path (Join-Path $root $f))) { throw "Missing: $f (run build.cmd first)" }
}
$crt = Get-ChildItem "${env:ProgramFiles(x86)}\Microsoft Visual Studio\*\*\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT", "$env:ProgramFiles\Microsoft Visual Studio\*\*\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT" -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -notmatch "debug|onecore" } | Sort-Object FullName | Select-Object -Last 1
if (-not $crt) { throw "Visual C++ redistributable folder not found" }

Remove-Item $payload -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory $payload | Out-Null
Copy-Item (Join-Path $root "build\diskpro-gui.exe") (Join-Path $payload "DiskPro.exe")
Copy-Item (Join-Path $root "build\diskpro-cli.exe"), (Join-Path $root "build\diskpro-inventory.exe"), (Join-Path $root "build\diskpro-tools.exe"), (Join-Path $root "build\diskpro-restore.exe"), (Join-Path $root "build\diskpro-clone.exe"), (Join-Path $root "build\diskpro-part.exe"), (Join-Path $root "build\diskpro-vhd.exe"), (Join-Path $root ".tools\slint\lib\slint_cpp.dll"),
    (Join-Path $root "README.md"), (Join-Path $root "LICENSE"), (Join-Path $root "THIRD_PARTY_NOTICES.md") $payload
Copy-Item (Join-Path $root ".tools\slint\licenses\LICENSE.md") (Join-Path $payload "SLINT-LICENSE.md")
Copy-Item (Join-Path $root "ROADMAP.md") $payload
Copy-Item (Join-Path $root "docs") (Join-Path $payload "docs") -Recurse
Get-ChildItem $crt.FullName -Filter *.dll | Where-Object { $_.Name -notin "vccorlib140.dll", "concrt140.dll" } | Copy-Item -Destination $payload

$iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw "Inno Setup 6 is not installed" }

& $iscc "/DAppVersion=$version" (Join-Path $PSScriptRoot "DiskPro.iss")
if ($LASTEXITCODE -ne 0) { throw "ISCC failed" }
$setup = Join-Path $root "dist\DiskPro-Setup-$version.exe"
$hash = (Get-FileHash $setup).Hash
Add-Content (Join-Path $root "dist\SHA256SUMS.txt") "$hash  DiskPro-Setup-$version.exe"
Get-Item $setup | Select-Object Name, @{n = "MB"; e = { [math]::Round($_.Length / 1MB, 1) } }


