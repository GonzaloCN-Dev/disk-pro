# Changelog

## 0.1.0 (unreleased candidate) – read-only MVP
### Added
- MBR/GPT analyzer (CRC checks, backup header, EBR chain with loop guard, overlap/range checks, 512/4096 sectors, NTFS/FAT/exFAT detection, fixed-VHD footer handling).
- Win32 read-only inventory (physical/virtual disks, partitions, drive letters, file system, system-disk detection, masked serial).
- Health: NVMe SMART/Health log and generic predict-failure flag; "not supported" otherwise.
- Partition map model with free-space gaps, overlap/alignment diagnostics.
- Slint GUI: disk list, graphical partition map, partition table, health, diagnostics, filter, light/dark/system theme, Spanish/English, density, keyboard navigation, loading/error/empty states, About.
- CLI tools: `diskpro-cli` (image analysis), `diskpro-inventory`.
- Unit tests (synthetic images, I/O failure injection) and VHD integration test (GPT and MBR).
- Tools page (GUI) and `diskpro-tools` CLI, read-only: SHA-256, disk/file imaging (never overwrites, bad blocks zero-filled and counted), surface scan, hex viewer with sector navigation, progress and cancel.
- Portable zip.
- Windows installer (Inno Setup 6, Spanish, per-user by default, optional all-users, bundles the VC++ runtime, uninstall offers to remove settings), same style as Rclone Lite and Db Pro. Not code-signed.
### Not included yet
Write operations, cloning, image restore, recovery, ATA SMART attributes, VHDX-specific tests, sector 4096 on real devices.


