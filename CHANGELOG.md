# Changelog

## 0.3.0 - Settings and updater
### Added
- Settings dialog: check for updates, download and install, optional check on start (off by default, at most once a day). One HTTPS GET to github.com; the installer comes only from official release assets, is limited in size and verified with SHA-256 (from the asset digest or SHA256SUMS.txt) before it runs; a failed check deletes the file. Setup relaunches the app.
- Version is now read from CMake, not hard-coded in the UI.
- Unit tests for release parsing, version comparison and refusal cases (no hash, foreign host, http, prerelease, malformed JSON).
### Verified
- Real download of the 0.2.1 installer from GitHub: size, SHA-256 match, wrong hash rejected and deleted, foreign and http URLs refused.
### Not verified
- The full in-app flow (update, installer run, relaunch) needs a newer release than the running version, so it can only be tried after this release exists.
## 0.2.1 - restore from the GUI
### Added
- Tools page: "Restore table (VHD only)" with preview, typed confirmation and apply, using the same engine and safety rules as `diskpro-restore` (system disk, non-virtual, >4 GiB, size mismatch and mounted volumes are blocked; apply re-checks the selected disk).
### Changed
- The window notice no longer says nothing is modified; it states that only VHD restore writes.
### Not verified
- The GUI restore flow has only been built and rendered, not exercised on a real VHD. The same engine passed the elevated VHD test through `diskpro-restore`.

## 0.2.0 - first write operation (partition table restore, virtual disks only)
### Added
- `diskpro-restore`: restores a `.ptb` partition-table backup (first/last MiB) onto a VIRTUAL disk. Preview by default; applying needs the typed text `RESTORE DISK N`; writes are flushed and verified by reading back.
- Write engine in `src/core/restore.*`, separate from the read-only code. Blocks: system disk, non-virtual disks, disks over 4 GiB, size or sector-size mismatch, disks with mounted volumes. Refuses tampered plans and out-of-range writes.
- Backups now record the disk identifier. Backup creation and `ptverify` unchanged.
- Unit tests (restore, safety rules, failing and lying sinks) and elevated VHD test (`tests/vhdx/run_restore_all.ps1`, GPT and MBR: passed).
### Fixed
- The 0.1.0 portable zip did not contain `diskpro-tools.exe`; it is now included, with `diskpro-restore.exe`.
- The restore preview no longer lists ranges to write when the operation is blocked.
### Not included yet
Restore has no GUI. It does not restore partition contents. VHDX, 4096-byte sectors and physical removable disks are untested. Cloning, image restore, recovery, partition create/delete/resize and ATA SMART are still pending.

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
- Partition table backup (first/last MiB with SHA-256, never overwrites) and `ptverify` integrity check, in the Tools page and `diskpro-tools`. Restore is NOT implemented.
- Portable zip.
- Windows installer (Inno Setup 6, Spanish, per-user by default, optional all-users, bundles the VC++ runtime, uninstall offers to remove settings), same style as Rclone Lite and Db Pro. Not code-signed.
### Not included yet
Write operations, cloning, image restore, recovery, ATA SMART attributes, VHDX-specific tests, sector 4096 on real devices.






