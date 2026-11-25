# Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
# Integration test on a DISPOSABLE fixed VHD (raw content + 512B footer) only. Requires elevation.
# Safety: operates exclusively on the disk obtained by mounting the VHDX created here.
param(
    [Parameter(Mandatory)][string]$InventoryExe,
    [Parameter(Mandatory)][string]$CliExe,
    [ValidateSet('GPT', 'MBR')][string]$Style = 'GPT',
    [int]$SizeMB = 128
)
$ErrorActionPreference = 'Stop'
$dir = Join-Path $env:TEMP 'opencode\diskpro-test'
New-Item -ItemType Directory -Force $dir | Out-Null
$vhd = Join-Path $dir "test-$Style-$([guid]::NewGuid().ToString('N').Substring(0,8)).vhd"
$log = Join-Path $dir "result-$Style.txt"
$dp = Join-Path $dir 'create.txt'
$disk = $null
try {
    if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Needs elevation' }
    Set-Content $dp "create vdisk file=`"$vhd`" maximum=$SizeMB type=fixed"
    diskpart /s $dp | Out-Null
    if (-not (Test-Path $vhd)) { throw 'VHDX not created' }
    Mount-DiskImage -ImagePath $vhd | Out-Null
    $img = Get-DiskImage -ImagePath $vhd
    $disk = Get-Disk -Number $img.Number
    # Hard safety checks: must be a file-backed virtual disk, never the boot/system disk.
    if ($disk.IsBoot -or $disk.IsSystem -or $disk.BusType -notin 'File Backed Virtual', 'Virtual') { throw "Refusing: disk $($disk.Number) is not a disposable virtual disk" }
    if ($disk.Size -gt 1GB) { throw 'Refusing: unexpected disk size' }
    Initialize-Disk -Number $disk.Number -PartitionStyle $Style | Out-Null
    $p1 = New-Partition -DiskNumber $disk.Number -Size 40MB
    Format-Volume -Partition $p1 -FileSystem NTFS -NewFileSystemLabel 'DPTEST' -Confirm:$false | Out-Null
    $p2 = New-Partition -DiskNumber $disk.Number -Size 40MB
    Format-Volume -Partition $p2 -FileSystem FAT32 -NewFileSystemLabel 'DPFAT' -Confirm:$false | Out-Null
    $out = & $InventoryExe | Out-String
    $out | Set-Content $log
    Dismount-DiskImage -ImagePath $vhd | Out-Null
    $disk = $null
    $cli = & $CliExe $vhd | Out-String
    Add-Content $log "`n--- CLI on VHDX file ($Style) ---`n$cli"
    $ok = $out -match '\[virtual\]|FileBackedVirtual' -and $cli -match "Scheme: $Style" -and $cli -match 'NTFS' -and $cli -match 'FAT32'
    Add-Content $log ("RESULT: " + $(if ($ok) { 'PASS' } else { 'FAIL' }))
}
catch { Add-Content $log "RESULT: ERROR $_" }
finally {
    if ($disk) { Dismount-DiskImage -ImagePath $vhd -ErrorAction SilentlyContinue | Out-Null }
    Remove-Item $vhd, $dp -Force -ErrorAction SilentlyContinue
}

