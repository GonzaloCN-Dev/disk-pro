# Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
# Integration test of partition-table editing on a DISPOSABLE fixed VHD. Requires elevation.
# Flow: blank VHD -> negative checks -> init GPT -> create x2 -> verify with Windows (Get-Partition) -> type/resize/merge/delete -> repeat for MBR.
param([Parameter(Mandatory)][string]$PartExe, [int]$SizeMB = 128)
$ErrorActionPreference = 'Stop'
$dir = Join-Path $env:TEMP 'opencode\diskpro-part-test'
New-Item -ItemType Directory -Force $dir | Out-Null
$id = [guid]::NewGuid().ToString('N').Substring(0, 8)
$vhd = Join-Path $dir "t-$id.vhd"; $log = Join-Path $dir 'result.txt'; $dp = Join-Path $dir 'create.txt'
$steps = @()
function Step($name, $ok) { $script:steps += [pscustomobject]@{ Step = $name; Ok = [bool]$ok } }
function Parts($n) { (Get-Partition -DiskNumber $n -ErrorAction SilentlyContinue | Sort-Object PartitionNumber | ForEach-Object { "$($_.Offset / 1MB):$($_.Size / 1MB)" }) -join ';' }
function Part($n, [string[]]$a) { $o = & $PartExe --disk $n @a 2>&1 | Out-String; Add-Content $log "--- $($a -join ' ')`n$o"; $o }
function Rescan {
    Update-HostStorageCache; Start-Sleep 2
    # Windows auto-mounts new partitions; Disk Pro refuses to edit a disk with drive letters, so release them (test setup only)
    Get-Partition -DiskNumber $n -ErrorAction SilentlyContinue | Where-Object { $_.DriveLetter } | ForEach-Object { Remove-PartitionAccessPath -DiskNumber $n -PartitionNumber $_.PartitionNumber -AccessPath "$($_.DriveLetter):\" -ErrorAction SilentlyContinue }
    Start-Sleep 1; Add-Content $log ("windows now: " + (Parts $n)) }
try {
    if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Needs elevation' }
    Set-Content $dp "create vdisk file=`"$vhd`" maximum=$SizeMB type=fixed"; diskpart /s $dp | Out-Null
    Mount-DiskImage -ImagePath $vhd | Out-Null
    $d = Get-Disk -Number (Get-DiskImage -ImagePath $vhd).Number; $n = $d.Number
    if ($d.IsBoot -or $d.IsSystem -or $d.BusType -notin 'File Backed Virtual', 'Virtual' -or $d.Size -gt 1GB) { throw "Refusing: disk $n is not disposable" }
    Set-Disk -Number $n -IsOffline $false -ErrorAction SilentlyContinue

    $sys = (Get-Disk | Where-Object { $_.IsBoot -or $_.IsSystem } | Select-Object -First 1).Number
    $neg = Part $sys @('init-gpt')
    Step 'system disk is BLOCKED' ($neg -match 'BLOCKED' -and $neg -match 'SYSTEM' -and $neg -notmatch 'WILL WRITE')
    $prev = Part $n @('init-gpt')
    Step 'preview shows writes, writes nothing' ($prev -match 'WILL WRITE' -and $prev -match 'Nothing written' -and (Get-Disk -Number $n).PartitionStyle -eq 'RAW')
    $bad = Part $n @('init-gpt', '--confirm', 'wrong')
    Step 'wrong confirmation writes nothing' ($bad -match 'does not match' -and (Get-Disk -Number $n).PartitionStyle -eq 'RAW')

    $r = Part $n @('init-gpt', '--confirm', "EDIT DISK $n"); Add-Content $log "--- init-gpt`n$r"; Rescan
    Step 'init GPT: OK and Windows sees GPT' ($r -match 'RESULT: OK' -and (Get-Disk -Number $n).PartitionStyle -eq 'GPT')
    Rescan
    $r = Part $n @('create', '--start', '2048', '--sectors', '40960', '--type', 'ntfs', '--name', 'Uno', '--confirm', "EDIT DISK $n"); Add-Content $log "--- create 1`n$r"
    Rescan
    $r2 = Part $n @('create', '--start', '45056', '--sectors', '81920', '--type', 'linux', '--name', 'Dos', '--confirm', "EDIT DISK $n"); Add-Content $log "--- create 2`n$r2"; Rescan
    Step 'create x2: OK' ($r -match 'RESULT: OK' -and $r2 -match 'RESULT: OK')
    $g = Parts $n
    Step "Windows sees the exact geometry ($g)" ($g -eq '1:20;22:40')
    $o = Part $n @('create', '--start', '4096', '--sectors', '2048', '--confirm', "EDIT DISK $n")
    Step 'overlapping create is BLOCKED' ($o -match 'BLOCKED' -and $o -match 'overlaps')
    $r = Part $n @('resize', '--index', '1', '--start', '2048', '--sectors', '43008', '--confirm', "EDIT DISK $n"); Rescan
    Step 'resize entry: OK and Windows agrees' ($r -match 'RESULT: OK' -and (Parts $n) -eq '1:21;22:40')
    $r = Part $n @('merge', '--index', '1', '--index2', '2', '--confirm', "EDIT DISK $n"); Rescan
    Step 'merge contiguous: OK and Windows agrees' ($r -match 'RESULT: OK' -and (Parts $n) -eq '1:61')
    $r = Part $n @('delete', '--index', '1', '--confirm', "EDIT DISK $n"); Rescan
    Step 'delete: OK and Windows sees no partitions' ($r -match 'RESULT: OK' -and -not (Get-Partition -DiskNumber $n -ErrorAction SilentlyContinue))

    $r = Part $n @('init-mbr', '--confirm', "EDIT DISK $n"); Rescan
    Step 'init MBR: OK and Windows sees MBR' ($r -match 'RESULT: OK' -and (Get-Disk -Number $n).PartitionStyle -eq 'MBR')
    $r = Part $n @('create', '--start', '2048', '--sectors', '40960', '--type', 'fat32', '--confirm', "EDIT DISK $n")
    Rescan
    $r2 = Part $n @('create', '--start', '45056', '--sectors', '40960', '--type', 'ntfs', '--confirm', "EDIT DISK $n")
    Rescan
    $r3 = Part $n @('setactive', '--index', '1', '--confirm', "EDIT DISK $n"); Rescan
    Step 'MBR create x2 + active: OK' ($r -match 'RESULT: OK' -and $r2 -match 'RESULT: OK' -and $r3 -match 'RESULT: OK')
    $mp = Get-Partition -DiskNumber $n | Sort-Object PartitionNumber
    Step "Windows sees MBR geometry and flags ($(Parts $n))" ((Parts $n) -eq '1:20;22:20' -and $mp[0].IsActive -and -not $mp[1].IsActive)
    # the table written by Disk Pro must be accepted by Windows for a real format
    $mp[1] | Add-PartitionAccessPath -AssignDriveLetter -ErrorAction SilentlyContinue
    $f = Format-Volume -Partition (Get-Partition -DiskNumber $n -PartitionNumber $mp[1].PartitionNumber) -FileSystem NTFS -Confirm:$false -ErrorAction SilentlyContinue
    Step 'Windows can format a partition created by Disk Pro' ($null -ne $f -and $f.FileSystem -eq 'NTFS')

    # ---- format FAT32 (own implementation), validated by Windows ----
    Rescan
    $r = Part $n @('init-gpt', '--confirm', "EDIT DISK $n"); Rescan
    Step 'GPT re-initialised before format' ((Get-Disk -Number $n).PartitionStyle -eq 'GPT')
    $r = Part $n @('create', '--start', '2048', '--sectors', '163840', '--type', 'fat32', '--name', 'F', '--confirm', "EDIT DISK $n"); Rescan
    $fp = Part $n @('format', '--index', '1', '--label', 'dpro', '--confirm', "FORMAT DISK $n PARTITION 1"); Add-Content $log $fp
    Step 'format FAT32: OK' ($fp -match 'RESULT: OK')
    $wrong = Part $n @('format', '--index', '1', '--confirm', 'nope')
    Step 'format with wrong confirmation writes nothing' ($wrong -match 'does not match')
    Start-Sleep 1; Update-HostStorageCache; Start-Sleep 2
    $pp = Get-Partition -DiskNumber $n -PartitionNumber 1 -ErrorAction SilentlyContinue
    if ($pp -and -not $pp.DriveLetter) { Add-PartitionAccessPath -DiskNumber $n -PartitionNumber 1 -AssignDriveLetter -ErrorAction SilentlyContinue; $pp = Get-Partition -DiskNumber $n -PartitionNumber 1 }
    $vol = if ($pp.DriveLetter) { Get-Volume -DriveLetter $pp.DriveLetter -ErrorAction SilentlyContinue } else { $null }
    Add-Content $log ("volume: " + ($vol | Out-String))
    Step 'Windows mounts it as FAT32 with label DPRO' ($vol -and $vol.FileSystem -eq 'FAT32' -and $vol.FileSystemLabel -eq 'DPRO')
    if ($vol) {
        $dl = $pp.DriveLetter
        $data = New-Object byte[] (4MB); (New-Object Random 7).NextBytes($data)
        [IO.File]::WriteAllBytes("${dl}:\test.bin", $data); Write-VolumeCache -DriveLetter $dl
        $h1 = (Get-FileHash "${dl}:\test.bin" -Algorithm SHA256).Hash
        $expected = (Get-FileHash -InputStream ([IO.MemoryStream]::new($data)) -Algorithm SHA256).Hash
        Step 'file written and read back on the formatted volume' ($h1 -eq $expected)
        $chk = (chkdsk "${dl}:" 2>&1 | Out-String); Add-Content $log $chk
        Step 'chkdsk (read-only) finds no problems' ($LASTEXITCODE -eq 0)
    }

    # ---- format exFAT (own implementation), validated by Windows ----
    Get-Partition -DiskNumber $n -ErrorAction SilentlyContinue | Where-Object { $_.DriveLetter } | ForEach-Object { Remove-PartitionAccessPath -DiskNumber $n -PartitionNumber $_.PartitionNumber -AccessPath "$($_.DriveLetter):\" -ErrorAction SilentlyContinue }
    Rescan
    # Windows keeps the previous volume mounted and refuses raw writes over it, so start from a fresh table
    $null = Part $n @('init-gpt', '--confirm', "EDIT DISK $n"); Rescan
    $null = Part $n @('create', '--start', '2048', '--sectors', '163840', '--type', 'ntfs', '--confirm', "EDIT DISK $n"); Rescan
    $xp = Part $n @('format', '--index', '1', '--fs', 'exfat', '--label', 'exdp', '--confirm', "FORMAT DISK $n PARTITION 1"); Add-Content $log $xp
    Step 'format exFAT: OK' ($xp -match 'RESULT: OK')
    Start-Sleep 1; Update-HostStorageCache; Start-Sleep 2
    $pp = Get-Partition -DiskNumber $n -PartitionNumber 1 -ErrorAction SilentlyContinue
    if ($pp -and -not $pp.DriveLetter) { Add-PartitionAccessPath -DiskNumber $n -PartitionNumber 1 -AssignDriveLetter -ErrorAction SilentlyContinue; $pp = Get-Partition -DiskNumber $n -PartitionNumber 1 }
    $vol = if ($pp.DriveLetter) { Get-Volume -DriveLetter $pp.DriveLetter -ErrorAction SilentlyContinue } else { $null }
    Add-Content $log ("exfat volume: " + ($vol | Out-String))
    Step 'Windows mounts it as exFAT with label EXDP' ($vol -and $vol.FileSystem -eq 'exFAT' -and $vol.FileSystemLabel -eq 'EXDP')
    if ($vol) {
        $dl = $pp.DriveLetter
        $data = New-Object byte[] (4MB); (New-Object Random 9).NextBytes($data)
        [IO.File]::WriteAllBytes("${dl}:\test.bin", $data); Write-VolumeCache -DriveLetter $dl
        $expected = (Get-FileHash -InputStream ([IO.MemoryStream]::new($data)) -Algorithm SHA256).Hash
        Step 'exFAT: file written and read back' ((Get-FileHash "${dl}:\test.bin" -Algorithm SHA256).Hash -eq $expected)
        $chk = (chkdsk "${dl}:" 2>&1 | Out-String); Add-Content $log $chk
        Step 'exFAT: chkdsk (read-only) finds no problems' ($LASTEXITCODE -eq 0)
    }
}
catch { Add-Content $log "ERROR: $_"; Step "exception: $_" $false }
finally {
    Dismount-DiskImage -ImagePath $vhd -ErrorAction SilentlyContinue | Out-Null
    Remove-Item $vhd, $dp -Force -ErrorAction SilentlyContinue
    $steps | Format-Table -AutoSize | Out-String | Tee-Object -Append -FilePath $log | Write-Host
    $allOk = ($steps.Count -gt 0) -and -not ($steps | Where-Object { -not $_.Ok })
    Add-Content $log ("RESULT: " + $(if ($allOk) { 'PASS' } else { 'FAIL' }))
    Write-Host ("RESULT: " + $(if ($allOk) { 'PASS' } else { 'FAIL' }))
}