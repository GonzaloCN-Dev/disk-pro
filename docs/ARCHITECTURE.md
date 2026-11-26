# Architecture

```
ui/main.slint            Slint UI (declarative, no logic)
src/app                  GUI glue: worker thread, string tables ES/EN, settings
src/core                 Portable, read-only: ByteSource, MBR/GPT analyzer, map model
src/platform/win         Win32: inventory (IOCTL, access 0), health (NVMe log / predict-failure)
src/cli                  Command line front ends
tests                    Unit tests + VHD integration scripts (disposable disks only)
```

## Decisions
- **Read-only by construction:** devices are opened with access 0 (metadata IOCTLs); image files with `"rb"`. No write engine exists. A future write engine will be a separate, elevated process (preview, typed confirmation, system-disk block, volume lock, range validation).
- **Portable core:** `core` has no OS headers; `ByteSource` abstracts devices/images so failure injection is testable.
- **UI thread never blocks:** enumeration and health run on a worker thread; results are posted via `invoke_from_event_loop`; stale results are discarded by a generation counter.
- **Privacy:** no telemetry; serials are masked; settings are 3 integers in `%APPDATA%\DiskPro\settings.ini`, validated on load.
- **UI licensing:** Slint Royalty-free License 2.0 (attribution via AboutSlint). See THIRD_PARTY_NOTICES.md.

## Known limits
- Disk list is not virtualized for thousands of rows (ListView is lazy but data is rebuilt wholesale); fine for realistic disk counts.
- Win32 inventory is covered by manual/VHD tests, not unit tests.
- UI font glyph fallback for some symbols depends on the system.

## Product roadmap

The product plan, phases, acceptance criteria, and safety gates are maintained in [`ROADMAP.md`](../ROADMAP.md). The roadmap does not authorize physical-disk writes or relax the current VHD size limit.

## Risks
Data loss in write phases; FS-aware resize needs a library with a compatible license (else unsupported, never simulated); SMART varies by driver/bridge.
