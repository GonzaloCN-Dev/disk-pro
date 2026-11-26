// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
// diskpro-clone: clones a disk (or an image file) onto a VIRTUAL disk. Preview unless --confirm is given.
//   diskpro-clone (--disk N | --image file) --to M [--confirm "CLONE TO DISK M"]
// Needs administrator. Refuses: system disk, non-virtual target, target > 4 GiB, mounted volumes, source larger than target.
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include "clone.h"
#include "device.h"
#include "inventory.h"

using namespace diskpro;

int main(int argc, char** argv) {
    long srcDisk = -1, dstDisk = -1;
    std::string image, typed;
    for (int i = 1; i + 1 < argc; ++i) {
        if (!std::strcmp(argv[i], "--disk")) srcDisk = std::strtol(argv[i + 1], nullptr, 10);
        else if (!std::strcmp(argv[i], "--image")) image = argv[i + 1];
        else if (!std::strcmp(argv[i], "--to")) dstDisk = std::strtol(argv[i + 1], nullptr, 10);
        else if (!std::strcmp(argv[i], "--confirm")) typed = argv[i + 1];
    }
    if (dstDisk < 0 || (srcDisk < 0) == image.empty()) {
        std::fprintf(stderr, "usage: diskpro-clone (--disk N | --image file) --to M [--confirm \"CLONE TO DISK M\"]\n");
        return 2;
    }
    const win::DiskInfo* disk = nullptr;
    auto disks = win::enumerateDisks();
    for (auto& d : disks) if (d.number == uint32_t(dstDisk)) disk = &d;
    if (!disk) { std::fprintf(stderr, "target disk %ld not found\n", dstDisk); return 1; }

    std::unique_ptr<ByteSource> src;
    std::string label;
    if (srcDisk >= 0) {
        auto d = std::make_unique<DeviceSource>(static_cast<int>(srcDisk));
        if (!d->ok()) { std::fprintf(stderr, "cannot open source disk %ld (administrator?)\n", srcDisk); return 1; }
        src = std::move(d); label = "disk " + std::to_string(srcDisk);
    } else {
        auto f = std::make_unique<FileSource>(image);
        if (!f->ok()) { std::fprintf(stderr, "cannot open image %s\n", image.c_str()); return 1; }
        src = std::move(f); label = image;
    }
    DeviceSource back{static_cast<int>(dstDisk)};
    if (!back.ok()) { std::fprintf(stderr, "cannot open target disk %ld (administrator?)\n", dstDisk); return 1; }

    CloneSourceFacts sf;
    sf.size = src->size(); sf.label = label; sf.isTargetItself = (srcDisk == dstDisk);
    sf.sectorSize = disk->logicalSector ? disk->logicalSector : 512;
    if (srcDisk >= 0) for (auto& d : disks) if (d.number == uint32_t(srcDisk)) sf.sectorSize = d.logicalSector ? d.logicalSector : 512;
    TargetFacts t;
    t.number = disk->number; t.size = back.size(); t.sectorSize = disk->logicalSector ? disk->logicalSector : 512;
    t.isSystemDisk = disk->isSystemDisk; t.isVirtual = disk->isVirtual;
    auto plan = planClone(sf, t);
    if (!disk->volumes.empty()) { plan.blockers.push_back("The target has mounted volumes (drive letters): unmount/clear them first."); plan.allowed = false; plan.bytes = 0; }

    std::printf("=== PREVIEW ===\nSource: %s  %.2f MiB\nTarget: disk %u  %s %s  bus=%s  %.2f MiB\n", label.c_str(), double(sf.size) / 1048576.0,
                disk->number, disk->vendor.c_str(), disk->product.c_str(), disk->busType.c_str(), double(t.size) / 1048576.0);
    if (plan.allowed) std::printf("WILL WRITE: offset 0, %llu bytes\n", (unsigned long long)plan.bytes);
    for (auto& w : plan.warnings) std::printf("WARNING: %s\n", w.c_str());
    for (auto& b : plan.blockers) std::printf("BLOCKED: %s\n", b.c_str());
    std::printf("Reversible? No: the target contents are overwritten.\n");
    if (!plan.allowed) { std::printf("RESULT: not allowed\n"); return 3; }
    if (typed.empty()) { std::printf("Nothing written. To apply, run again with: --confirm \"%s\"\n", plan.token.c_str()); return 0; }
    if (typed != plan.token) { std::printf("Confirmation text does not match. Nothing written.\n"); return 3; }

    DeviceSink sink{static_cast<int>(dstDisk)};
    if (!sink.ok()) { std::fprintf(stderr, "cannot open target for writing (administrator?)\n"); return 1; }
    int lastPct = -1, lastPhase = -1;
    auto res = applyClone(*src, sink, back, plan, typed, [&](int ph, uint64_t d, uint64_t tot) {
        int pct = tot ? int(d * 100 / tot) : 0;
        if (pct != lastPct || ph != lastPhase) { std::printf("%s %3d%%\n", ph == 0 ? "copying  " : "verifying", pct); std::fflush(stdout); lastPct = pct; lastPhase = ph; }
        return true;
    });
    sink.refreshLayout();
    if (!res.ok) { std::printf("RESULT: FAILED: %s%s\n", res.error.c_str(), res.written ? " (the target may be partially written)" : ""); return 1; }
    std::printf("RESULT: OK (%llu bytes copied, flushed and verified by reading back)\n", (unsigned long long)res.copied);
    return 0;
}