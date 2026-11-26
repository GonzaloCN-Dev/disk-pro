// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
// diskpro-restore: restores a partition-table backup (.ptb) onto a VIRTUAL disk. Preview unless --confirm is given.
//   diskpro-restore <backup.ptb> <disk number> [--confirm "RESTORE DISK N"]
// Needs administrator. Refuses: system disk, non-virtual disks, disks > 4 GiB, disks with mounted volumes, size mismatch.
#include <cstdio>
#include <cstring>
#include <string>
#include "device.h"
#include "inventory.h"
#include "restore.h"

using namespace diskpro;

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: diskpro-restore <backup.ptb> <disk number> [--confirm \"RESTORE DISK N\"]\n"); return 2; }
    char* e = nullptr;
    long n = std::strtol(argv[2], &e, 10);
    if (*e || n < 0) { std::fprintf(stderr, "invalid disk number\n"); return 2; }
    std::string typed;
    for (int i = 3; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--confirm") && i + 1 < argc) typed = argv[++i];
        else { std::fprintf(stderr, "unknown or incomplete argument: %s\n", argv[i]); return 2; }
    }

    auto bak = loadPartitionBackup(argv[1]);
    const win::DiskInfo* disk = nullptr;
    auto disks = win::enumerateDisks();
    for (auto& d : disks) if (d.number == uint32_t(n)) disk = &d;
    if (!disk) { std::fprintf(stderr, "disk %ld not found\n", n); return 1; }

    DeviceSource src{static_cast<int>(n)};
    if (!src.ok()) { std::fprintf(stderr, "cannot open disk %ld for reading (run as administrator)\n", n); return 1; }
    auto cur = analyze(src);
    TargetFacts t;
    t.number = disk->number; t.size = src.size(); t.sectorSize = disk->logicalSector ? disk->logicalSector : 512;
    t.isSystemDisk = disk->isSystemDisk; t.isVirtual = disk->isVirtual;
    t.currentScheme = schemeName(cur.scheme); t.currentDiskId = cur.diskId;
    auto plan = planRestore(bak, t);
    if (!disk->volumes.empty()) { plan.blockers.push_back("The disk has mounted volumes (drive letters): unmount/clear them first."); plan.allowed = false; plan.ranges.clear(); }

    std::printf("=== PREVIEW ===\nTarget: disk %u  %s %s  %s  bus=%s  %.2f MiB\n", disk->number, disk->vendor.c_str(), disk->product.c_str(),
                disk->style.c_str(), disk->busType.c_str(), double(t.size) / 1048576.0);
    std::printf("Current table: %s (%zu partitions)\nBackup: scheme=%s id=%s size=%llu\n", t.currentScheme.c_str(), disk->partitions.size(),
                bak.scheme.c_str(), bak.diskId.c_str(), (unsigned long long)bak.diskSize);
    for (auto& r : plan.ranges) std::printf("WILL WRITE: offset %llu, %llu bytes\n", (unsigned long long)r.offset, (unsigned long long)r.length);
    for (auto& w : plan.warnings) std::printf("WARNING: %s\n", w.c_str());
    for (auto& b : plan.blockers) std::printf("BLOCKED: %s\n", b.c_str());
    std::printf("Reversible? Only if you keep a backup of the CURRENT table: this operation overwrites it.\n");
    if (!plan.allowed) { std::printf("RESULT: not allowed\n"); return 3; }
    if (typed.empty()) { std::printf("Nothing written. To apply, run again with: --confirm \"%s\"\n", plan.token.c_str()); return 0; }
    if (typed != plan.token) { std::printf("Confirmation text does not match. Nothing written.\n"); return 3; }

    DeviceSink sink{static_cast<int>(n), t};
    if (!sink.ok()) { std::fprintf(stderr, "cannot open disk for writing (administrator?)\n"); return 1; }
    auto res = applyRestore(bak, plan, typed, sink, src);
    sink.refreshLayout();
    if (!res.ok) { std::printf("RESULT: FAILED: %s%s\n", res.error.c_str(), res.written ? " (the disk may be partially written)" : ""); return 1; }
    std::printf("RESULT: OK (written, flushed and verified by reading back)\n");
    return 0;
}


