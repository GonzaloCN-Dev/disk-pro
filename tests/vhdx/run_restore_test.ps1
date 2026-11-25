# Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
# Integration test of the partition-table restore on a DISPOSABLE fixed VHD only. Requires elevation.
# Flow: create VHD -> GPT + 2 partitions -> backup table -> wipe table -> restore -> verify -> negative checks.
param(
    [Parameter(Mandatory)][string]$ToolsExe,
    [Parameter(Mandatory)][string]$RestoreExe,
    [Parameter(Mandatory)][string]$CliExe,
    [ValidateSet('GPT', 'MBR')][string]$Style = 'GPT',
    [int]$SizeMB = 128
)
$ErrorActionPreference = 'Stop'
$dir = Join-Path $env:TEMP 'opencode\diskpro-restore-test'
New-Item -ItemType Directory -Force $dir | Out-Null
$id = [guid]::NewGuid().ToString('N').Substring(0, 8)
$vhd = Join-Path $dir "rt-$Style-$id.vhd"
$ptb = Join-Path $dir "rt-$Style-$id.ptb"
$log = Join-Path $dir "result-$Style.txt"
$dp = Join-Path $dir 'create.txt'
$disk = $null
$steps = @()
function Step($name, $ok) { $script:steps += [pscustomobject]@{ Step = $name; Ok = [bool]$ok } }
try {
    if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Needs elevation' }
    Set-Content $dp "create vdisk file=`"$vhd`" maximum=$SizeMB type=fixed"
    diskpart /s $dp | Out-Null
    Mount-DiskImage -ImagePath $vhd | Out-Null
    $disk = Get-Disk -Number (Get-DiskImage -ImagePath $vhd).Number
    if ($disk.IsBoot -or $disk.IsSystem -or $disk.BusType -notin 'File Backed Virtual', 'Virtual') { throw "Refusing: disk $($disk.Number) is not disposable" }
    if ($disk.Size -gt 1GB) { throw 'Refusing: unexpected disk size' }
    $n = $disk.Number
    Initialize-Disk -Number $n -PartitionStyle $Style | Out-Null
    $p1 = New-Partition -DiskNumber $n -Size 40MB -AssignDriveLetter; Format-Volume -Partition $p1 -FileSystem NTFS -Confirm:$false | Out-Null
    $p2 = New-Partition -DiskNumber $n -Size 40MB; Format-Volume -Partition $p2 -FileSystem FAT32 -Confirm:$false | Out-Null
    $before = (Get-Partition -DiskNumber $n | Sort-Object PartitionNumber | ForEach-Object { "$($_.PartitionNumber):$($_.Offset):$($_.Size)" }) -join ';'

    $o = & $ToolsExe ptbackup "disk:$n" $ptb 2>&1 | Out-String
    Step 'backup created' ((Test-Path $ptb) -and $o -match "scheme=$Style")
    Step 'backup verifies' ((& $ToolsExe ptverify $ptb 2>&1 | Out-String) -match '^OK')

    # Negative checks (preview only, nothing written)
    $sys = (Get-Disk | Where-Object { $_.IsBoot -or $_.IsSystem } | Select-Object -First 1).Number
    $neg = & $RestoreExe $ptb $sys 2>&1 | Out-String
    Add-Content $log "--- system disk preview ($sys)`n$neg"

    Step 'system disk is BLOCKED' ($neg -match 'BLOCKED' -and $neg -match 'SYSTEM' -and $neg -notmatch 'WILL WRITE')
    $neg2 = & $RestoreExe $ptb $n 2>&1 | Out-String
    Add-Content $log "--- mounted volumes preview ($n)`n$neg2"
    Step 'disk with mounted volumes is BLOCKED' ($neg2 -match 'mounted volumes' -and $neg2 -notmatch 'WILL WRITE')

    # Wipe the VHD's table, then restore it
    Clear-Disk -Number $n -RemoveData -RemoveOEM -Confirm:$false | Out-Null
    Start-Sleep 1
    $prev = & $RestoreExe $ptb $n 2>&1 | Out-String
    Step 'preview shows ranges, writes nothing' ($prev -match 'WILL WRITE' -and $prev -match 'Nothing written')
    $bad = & $RestoreExe $ptb $n --confirm 'wrong text' 2>&1 | Out-String
    Step 'wrong confirmation writes nothing' ($bad -match 'does not match')
    $res = & $RestoreExe $ptb $n --confirm "RESTORE DISK $n" 2>&1 | Out-String
    Step 'restore OK and verified' ($res -match 'RESULT: OK')

    Update-Disk -Number $n
    Start-Sleep 2
    $after = (Get-Partition -DiskNumber $n -ErrorAction SilentlyContinue | Sort-Object PartitionNumber | ForEach-Object { "$($_.PartitionNumber):$($_.Offset):$($_.Size)" }) -join ';'
    Step "partitions identical after restore ($before)" ($before -eq $after)
    Add-Content $log "before=$before`nafter=$after`n$res"
    Dismount-DiskImage -ImagePath $vhd | Out-Null; $disk = $null
    $cli = & $CliExe $vhd | Out-String
    Step 'CLI sees restored table' ($cli -match "Scheme: $Style")
}
catch { Add-Content $log "ERROR: $_"; Step "exception: $_" $false }
finally {
    if ($disk) { Dismount-DiskImage -ImagePath $vhd -ErrorAction SilentlyContinue | Out-Null }
    Remove-Item $vhd, $dp, $ptb -Force -ErrorAction SilentlyContinue
    $steps | Format-Table -AutoSize | Out-String | Tee-Object -Append -FilePath $log | Write-Host
    $allOk = ($steps.Count -gt 0) -and -not ($steps | Where-Object { -not $_.Ok })
    Add-Content $log ("RESULT: " + $(if ($allOk) { 'PASS' } else { 'FAIL' }))
    Write-Host ("RESULT ($Style): " + $(if ($allOk) { 'PASS' } else { 'FAIL' }))
}



