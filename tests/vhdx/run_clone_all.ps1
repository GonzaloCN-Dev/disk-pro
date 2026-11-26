# Disk Pro - Runs the clone integration test (elevated). Disposable VHDs only.
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File',"`"$PSCommandPath`""; return
}
$out = Join-Path $env:TEMP 'opencode\diskpro-clone-test\result.txt'
Remove-Item $out -ErrorAction SilentlyContinue
& (Join-Path $PSScriptRoot 'run_clone_test.ps1') -CloneExe (Join-Path $root 'build\diskpro-clone.exe') -CliExe (Join-Path $root 'build\diskpro-cli.exe')
Read-Host 'Enter to close'