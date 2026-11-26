# Changelog

## 0.5.0 - Partition editing, formatting and recovery
- Includes everything listed under the 0.4.0 notes below (never published separately), plus: redesigned window with menu bar, toolbar and right-click menu; `diskpro-tools recover` and a Tools button that look for lost NTFS/FAT32/exFAT partitions (read-only; unit-tested in memory and on a synthetic image, not on real lost disks).
- Not verified: GUI mouse interaction, installer, in-app update, physical disks, CI.

## 0.4.0 - Clone and interactive partition bar
### Added
- **Format as FAT32** (own implementation, quick format; `diskpro-part format` and the right-click menu). Writes only inside the chosen partition, confirmation `FORMAT DISK N PARTITION I`. exFAT and NTFS are NOT implemented.
- **Partition table editing** (diskpro-part, right-click menu and Tools page): new MBR/GPT table, create, delete, change type, mark active, resize/move and merge contiguous partitions. Only the table is written (never file systems); MBR primary partitions only; same write policy as clone/restore; typed confirmation `EDIT DISK N`; read-back verification and re-analysis.
- **Clone** (`diskpro-clone` and the Tools page): copies a disk or an image file onto a virtual disk with a progress bar, cancel button and read-back verification. Same policy as restore: never the system disk, target must be a VHD/VHDX of at most 4 GiB with no mounted volumes, source must fit, same sector size, typed confirmation `CLONE TO DISK N`. Needs administrator.
- **Interactive partition bar**: drag a partition edge with the mouse to resize, or its body to move, with neighbour limits, 1 MiB alignment and warnings. **Preview only: nothing is applied and no partition is changed.**
- Unit tests for the clone engine (plan, wrong token, cancel, write error, silent corruption) and for the drag model; integration test `tests\vhdx\run_clone_all.ps1`.
### Verified
- Real clone between two disposable 128 MB VHDs (elevated, `run_clone_all.ps1`): PASS 9/9 - system disk and same-disk blocked, preview and wrong confirmation write nothing, progress for copy and verify, partitions identical, and a file on the clone has the same SHA-256 as the original.
- Table editing on a disposable 128 MB VHD (elevated, `run_part_all.ps1`): PASS 26/26 (FAT32 and exFAT), Windows itself confirms the exact geometry (GPT and MBR), the active flag, formats a partition created by Disk Pro, mounts the FAT32 volume we formatted (label included), writes/reads a 4 MiB file on it and `chkdsk` finds no problems. Unit tests (memory, MBR/GPT, 512/4096): 9/9.
- 7/7 unit tests (before table editing). The drag logic was exercised in the real window by firing the bar's callbacks and checking the result on screen. Without administrator, the CLI opens no disk.
### Not verified
- Cloning from the GUI, real mouse drag (the automated test could not inject it), VHDX, 4096-byte sectors, physical disks.
### Not implemented
- Applying resize/move, creating or deleting partitions, cloning to physical disks, clone-and-grow.

## 0.3.1 - Export report
### Added
- **Format as FAT32** (own implementation, quick format; `diskpro-part format` and the right-click menu). Writes only inside the chosen partition, confirmation `FORMAT DISK N PARTITION I`. exFAT and NTFS are NOT implemented.
- **Partition table editing** (diskpro-part, right-click menu and Tools page): new MBR/GPT table, create, delete, change type, mark active, resize/move and merge contiguous partitions. Only the table is written (never file systems); MBR primary partitions only; same write policy as clone/restore; typed confirmation `EDIT DISK N`; read-back verification and re-analysis.
- **Export report** button: saves a plain-text report of the detected disks (size, bus, style, partitions, health, masked serial). Read-only; never overwrites an existing file.
- Unit test for the report formatting.
### Fixed
- Toolbar no longer clips the last buttons; the window minimum width was raised and the filter box shrinks first.
### Verified
- 5/5 unit tests. A real report generated from this machine's inventory was read back and checked.
### Not verified
- The in-app update flow (0.3.0 -> 0.3.1), installer install/uninstall, VHDX, 4096-byte sectors, physical removable disks.

## 0.3.0 - Settings and updater
### Added
- **Format as FAT32** (own implementation, quick format; `diskpro-part format` and the right-click menu). Writes only inside the chosen partition, confirmation `FORMAT DISK N PARTITION I`. exFAT and NTFS are NOT implemented.
- Settings dialog: check for updates, download and install, optional check on start (off by default, at most once a day). One HTTPS GET to github.com; the installer comes only from official release assets, is limited in size and verified with SHA-256 (from the asset digest or SHA256SUMS.txt) before it runs; a failed check deletes the file. Setup relaunches the app.
- Version is now read from CMake, not hard-coded in the UI.
- Unit tests for release parsing, version comparison and refusal cases (no hash, foreign host, http, prerelease, malformed JSON).
### Verified
- Real download of the 0.2.1 installer from GitHub: size, SHA-256 match, wrong hash rejected and deleted, foreign and http URLs refused.
### Not verified
- The full in-app flow (update, installer run, relaunch) needs a newer release than the running version, so it can only be tried after this release exists.
## 0.2.1 - restore from the GUI
### Added
- **Format as FAT32** (own implementation, quick format; `diskpro-part format` and the right-click menu). Writes only inside the chosen partition, confirmation `FORMAT DISK N PARTITION I`. exFAT and NTFS are NOT implemented.
- **Partition table editing** (diskpro-part, right-click menu and Tools page): new MBR/GPT table, create, delete, change type, mark active, resize/move and merge contiguous partitions. Only the table is written (never file systems); MBR primary partitions only; same write policy as clone/restore; typed confirmation `EDIT DISK N`; read-back verification and re-analysis.
- Tools page: "Restore table (VHD only)" with preview, typed confirmation and apply, using the same engine and safety rules as `diskpro-restore` (system disk, non-virtual, >4 GiB, size mismatch and mounted volumes are blocked; apply re-checks the selected disk).
### Changed
- The window notice no longer says nothing is modified; it states that only VHD restore writes.
### Not verified
- The GUI restore flow has only been built and rendered, not exercised on a real VHD. The same engine passed the elevated VHD test through `diskpro-restore`.

## 0.2.0 - first write operation (partition table restore, virtual disks only)
### Added
- **Format as FAT32** (own implementation, quick format; `diskpro-part format` and the right-click menu). Writes only inside the chosen partition, confirmation `FORMAT DISK N PARTITION I`. exFAT and NTFS are NOT implemented.
- **Partition table editing** (diskpro-part, right-click menu and Tools page): new MBR/GPT table, create, delete, change type, mark active, resize/move and merge contiguous partitions. Only the table is written (never file systems); MBR primary partitions only; same write policy as clone/restore; typed confirmation `EDIT DISK N`; read-back verification and re-analysis.
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
- **Format as FAT32** (own implementation, quick format; `diskpro-part format` and the right-click menu). Writes only inside the chosen partition, confirmation `FORMAT DISK N PARTITION I`. exFAT and NTFS are NOT implemented.
- **Partition table editing** (diskpro-part, right-click menu and Tools page): new MBR/GPT table, create, delete, change type, mark active, resize/move and merge contiguous partitions. Only the table is written (never file systems); MBR primary partitions only; same write policy as clone/restore; typed confirmation `EDIT DISK N`; read-back verification and re-analysis.
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






