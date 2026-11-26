# Disk Pro - Runs the partition-table integration test (elevated). Disposable VHD only.
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File',"`"$PSCommandPath`""; return
}
$out = Join-Path $env:TEMP 'opencode\diskpro-part-test\result.txt'
Remove-Item $out -ErrorAction SilentlyContinue
& (Join-Path $PSScriptRoot 'run_part_test.ps1') -PartExe (Join-Path $root 'build\diskpro-part.exe')
Read-Host 'Enter to close'