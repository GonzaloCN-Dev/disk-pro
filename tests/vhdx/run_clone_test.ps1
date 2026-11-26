# Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
# Integration test of disk cloning between two DISPOSABLE fixed VHDs. Requires elevation.
# Flow: source VHD (GPT + NTFS + file) -> blank target VHD -> negative checks -> clone -> verify partitions and file hash.
param(
    [Parameter(Mandatory)][string]$CloneExe,
    [Parameter(Mandatory)][string]$CliExe,
    [int]$SizeMB = 128
)
$ErrorActionPreference = 'Stop'
$dir = Join-Path $env:TEMP 'opencode\diskpro-clone-test'
New-Item -ItemType Directory -Force $dir | Out-Null
$id = [guid]::NewGuid().ToString('N').Substring(0, 8)
$vs = Join-Path $dir "src-$id.vhd"; $vt = Join-Path $dir "dst-$id.vhd"
$log = Join-Path $dir 'result.txt'; $dp = Join-Path $dir 'create.txt'
$mounted = @(); $steps = @()
function Step($name, $ok) { $script:steps += [pscustomobject]@{ Step = $name; Ok = [bool]$ok } }
function NewVhd($path) {
    Set-Content $dp "create vdisk file=`"$path`" maximum=$SizeMB type=fixed"; diskpart /s $dp | Out-Null
    Mount-DiskImage -ImagePath $path | Out-Null; $script:mounted += $path
    $d = Get-Disk -Number (Get-DiskImage -ImagePath $path).Number
    if ($d.IsBoot -or $d.IsSystem -or $d.BusType -notin 'File Backed Virtual', 'Virtual' -or $d.Size -gt 1GB) { throw "Refusing: disk $($d.Number) is not disposable" }
    return $d
}
try {
    if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Needs elevation' }
    $s = NewVhd $vs; $t = NewVhd $vt
    Initialize-Disk -Number $s.Number -PartitionStyle GPT | Out-Null
    $p = New-Partition -DiskNumber $s.Number -Size 60MB -AssignDriveLetter; Format-Volume -Partition $p -FileSystem NTFS -Confirm:$false | Out-Null
    $f = "$($p.DriveLetter):\data.bin"
    $bytes = New-Object byte[] (8MB); (New-Object Random 42).NextBytes($bytes); [IO.File]::WriteAllBytes($f, $bytes)
    $h1 = (Get-FileHash $f -Algorithm SHA256).Hash; Write-VolumeCache -DriveLetter $p.DriveLetter
    $before = (Get-Partition -DiskNumber $s.Number | Sort-Object PartitionNumber | ForEach-Object { "$($_.PartitionNumber):$($_.Offset):$($_.Size)" }) -join ';'
    $n = $t.Number; $sn = $s.Number

    $sys = (Get-Disk | Where-Object { $_.IsBoot -or $_.IsSystem } | Select-Object -First 1).Number
    $neg = & $CloneExe --disk $sn --to $sys 2>&1 | Out-String
    Add-Content $log "--- system disk as target ($sys)`n$neg"
    Step 'system disk target is BLOCKED' ($neg -match 'BLOCKED' -and $neg -match 'SYSTEM' -and $neg -notmatch 'WILL WRITE')
    $neg2 = & $CloneExe --disk $sn --to $sn 2>&1 | Out-String
    Step 'same disk as source and target is BLOCKED' ($neg2 -match 'same disk' -and $neg2 -notmatch 'WILL WRITE')

    $prev = & $CloneExe --disk $sn --to $n 2>&1 | Out-String
    Add-Content $log "--- preview`n$prev"
    Step 'preview shows range, writes nothing' ($prev -match 'WILL WRITE' -and $prev -match 'Nothing written')
    Step 'target still blank after preview' ((Get-Disk -Number $n).PartitionStyle -eq 'RAW')
    $bad = & $CloneExe --disk $sn --to $n --confirm 'wrong' 2>&1 | Out-String
    Step 'wrong confirmation writes nothing' ($bad -match 'does not match' -and (Get-Disk -Number $n).PartitionStyle -eq 'RAW')
    $res = & $CloneExe --disk $sn --to $n --confirm "CLONE TO DISK $n" 2>&1 | Out-String
    Add-Content $log "--- clone`n$res"
    Step 'progress is reported (copy and verify)' ($res -match 'copying\s+100%' -and $res -match 'verifying\s+100%')
    Step 'clone OK and verified' ($res -match 'RESULT: OK')

    # a GPT clone repeats the disk GUID: dismount the source first so Windows does not take the copy offline
    Dismount-DiskImage -ImagePath $vs | Out-Null; Dismount-DiskImage -ImagePath $vt | Out-Null; Start-Sleep 1
    Mount-DiskImage -ImagePath $vt | Out-Null
    $n = (Get-DiskImage -ImagePath $vt).Number; Start-Sleep 2
    $after = (Get-Partition -DiskNumber $n -ErrorAction SilentlyContinue | Sort-Object PartitionNumber | ForEach-Object { "$($_.PartitionNumber):$($_.Offset):$($_.Size)" }) -join ';'
    Step "partitions identical on the clone ($before)" ($before -eq $after)
    $q = Get-Partition -DiskNumber $n | Where-Object { $_.Type -eq 'Basic' } | Select-Object -First 1
    if ($q) {
        Add-PartitionAccessPath -DiskNumber $n -PartitionNumber $q.PartitionNumber -AssignDriveLetter -ErrorAction SilentlyContinue
        $q = Get-Partition -DiskNumber $n -PartitionNumber $q.PartitionNumber
        $h2 = if ($q.DriveLetter) { (Get-FileHash "$($q.DriveLetter):\data.bin" -Algorithm SHA256).Hash } else { '' }
    }
    Step 'file on the clone has the same SHA-256' ($h1 -and $h1 -eq $h2)
    Add-Content $log "before=$before`nafter=$after`nh1=$h1`nh2=$h2"
}
catch { Add-Content $log "ERROR: $_"; Step "exception: $_" $false }
finally {
    foreach ($m in ($mounted + $vs + $vt | Select-Object -Unique)) { Dismount-DiskImage -ImagePath $m -ErrorAction SilentlyContinue | Out-Null }
    Remove-Item $vs, $vt, $dp -Force -ErrorAction SilentlyContinue
    $steps | Format-Table -AutoSize | Out-String | Tee-Object -Append -FilePath $log | Write-Host
    $allOk = ($steps.Count -gt 0) -and -not ($steps | Where-Object { -not $_.Ok })
    Add-Content $log ("RESULT: " + $(if ($allOk) { 'PASS' } else { 'FAIL' }))
    Write-Host ("RESULT: " + $(if ($allOk) { 'PASS' } else { 'FAIL' }))
}