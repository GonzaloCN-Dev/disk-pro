# Run from an ELEVATED PowerShell in the repository root, after build.cmd.
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
Remove-Item "$env:TEMP\opencode\diskpro-restore-test\result-*.txt" -ErrorAction SilentlyContinue
foreach ($s in 'GPT', 'MBR') {
    & "$PSScriptRoot\run_restore_test.ps1" -ToolsExe "$root\build\diskpro-tools.exe" -RestoreExe "$root\build\diskpro-restore.exe" -CliExe "$root\build\diskpro-cli.exe" -Style $s
}
Get-Content "$env:TEMP\opencode\diskpro-restore-test\result-*.txt"
