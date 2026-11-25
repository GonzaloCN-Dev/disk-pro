# Run from an ELEVATED PowerShell in the repository root, after build.cmd.
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
Remove-Item "$env:TEMP\opencode\diskpro-test\result-*.txt" -ErrorAction SilentlyContinue
foreach ($s in 'GPT', 'MBR') {
    & "$PSScriptRoot\run_vhdx_test.ps1" -InventoryExe "$root\build\diskpro-inventory.exe" -CliExe "$root\build\diskpro-cli.exe" -Style $s
}
Get-Content "$env:TEMP\opencode\diskpro-test\result-*.txt"
