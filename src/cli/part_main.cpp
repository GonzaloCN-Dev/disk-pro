// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
// diskpro-part: edits the partition TABLE of a VIRTUAL disk. Preview unless --confirm is given.
//   diskpro-part --disk N <op> [options] [--confirm "EDIT DISK N"]
//   ops: init-mbr | init-gpt | create --start LBA --sectors N [--type T] [--name X]
//        delete --index I | settype --index I --type T | setactive --index I
//        resize --index I --start LBA --sectors N | merge --index I --index2 J\n//        format --index I [--fs fat32] [--label X]   (token: "FORMAT DISK N PARTITION I")
// Needs administrator. Refuses: system disk, non-virtual disk, > 4 GiB, mounted volumes. Never touches file systems.
#include <cstdio>
#include <cstring>
#include <string>
#include "device.h"
#include "inventory.h"
#include "format.h"
#include "partops.h"

using namespace diskpro;

int main(int argc, char** argv) {
    long disk = -1;
    std::string op, typed, type, name, label, fs = "fat32";
    long index = 0, index2 = 0;
    unsigned long long start = 0, sectors = 0;
    for (int i = 1; i < argc; ++i) {
        auto has = [&](const char* k) { return !std::strcmp(argv[i], k) && i + 1 < argc; };
        if (has("--disk")) disk = std::strtol(argv[++i], nullptr, 10);
        else if (has("--confirm")) typed = argv[++i];
        else if (has("--type")) type = argv[++i];
        else if (has("--name")) name = argv[++i];
        else if (has("--label")) label = argv[++i];
        else if (has("--fs")) fs = argv[++i];
        else if (has("--index")) index = std::strtol(argv[++i], nullptr, 10);
        else if (has("--index2")) index2 = std::strtol(argv[++i], nullptr, 10);
        else if (has("--start")) start = std::strtoull(argv[++i], nullptr, 10);
        else if (has("--sectors")) sectors = std::strtoull(argv[++i], nullptr, 10);
        else if (argv[i][0] != '-' && op.empty()) op = argv[i];
    }
    const bool isFormat = op == "format";
    PartOp p;
    using K = PartOp::Kind;
    if (op == "init-mbr") p.kind = K::InitMbr; else if (op == "init-gpt") p.kind = K::InitGpt;
    else if (op == "create") p.kind = K::Create; else if (op == "delete") p.kind = K::Delete;
    else if (op == "settype") p.kind = K::SetType; else if (op == "setactive") p.kind = K::SetActive;
    else if (op == "resize") p.kind = K::Resize; else if (op == "merge") p.kind = K::Merge;
    else if (isFormat) {}
    else {
        std::fprintf(stderr, "usage: diskpro-part --disk N (init-mbr|init-gpt|create|delete|settype|setactive|resize|merge|format) [options] [--confirm \"EDIT DISK N\"]\n");
        return 2;
    }
    if (disk < 0) { std::fprintf(stderr, "--disk is required\n"); return 2; }
    p.index = int(index); p.index2 = int(index2); p.firstLba = start; p.sectorCount = sectors; p.type = type; p.name = name;

    const win::DiskInfo* di = nullptr;
    auto disks = win::enumerateDisks();
    for (auto& d : disks) if (d.number == uint32_t(disk)) di = &d;
    if (!di) { std::fprintf(stderr, "disk %ld not found\n", disk); return 1; }
    DeviceSource src{static_cast<int>(disk)};
    if (!src.ok()) { std::fprintf(stderr, "cannot open disk %ld (administrator?)\n", disk); return 1; }
    TargetFacts t;
    t.number = di->number; t.size = src.size(); t.sectorSize = di->logicalSector ? di->logicalSector : 512;
    t.isSystemDisk = di->isSystemDisk; t.isVirtual = di->isVirtual;
    PartOpPlan plan;
    FormatPlan fplan;
    if (isFormat) {
        FormatOp fo; fo.index = int(index); fo.fs = fs; fo.label = label;
        fplan = planFormat(src, t, fo);
        plan.allowed = fplan.allowed; plan.blockers = fplan.blockers; plan.warnings = fplan.warnings; plan.writes = fplan.writes;
        plan.token = fplan.token; plan.summary = fplan.summary;
    } else plan = planPartOp(src, t, p);
    // Formatting locks and dismounts the volume of the chosen partition itself; table edits need the disk free of drive letters.
    if (!isFormat && !di->volumes.empty()) { plan.blockers.push_back("The disk has mounted volumes (drive letters): unmount/clear them first."); plan.allowed = false; }

    std::printf("=== PREVIEW ===\nDisk %u  %s %s  bus=%s  %.2f MiB  sector=%u\n", di->number, di->vendor.c_str(), di->product.c_str(),
                di->busType.c_str(), double(t.size) / 1048576.0, t.sectorSize);
    if (plan.allowed) {
        std::printf("OPERATION: %s\n", plan.summary.c_str());
        for (auto& w : plan.writes) std::printf("WILL WRITE: offset %llu, %zu bytes\n", (unsigned long long)w.offset, w.data.size());
    }
    for (auto& w : plan.warnings) std::printf("WARNING: %s\n", w.c_str());
    for (auto& b : plan.blockers) std::printf("BLOCKED: %s\n", b.c_str());
    if (!plan.allowed) { std::printf("RESULT: not allowed\n"); return 3; }
    if (typed.empty()) { std::printf("Nothing written. To apply, run again with: --confirm \"%s\"\n", plan.token.c_str()); return 0; }
    if (typed != plan.token) { std::printf("Confirmation text does not match. Nothing written.\n"); return 3; }
    DeviceSink sink{static_cast<int>(disk)};
    if (!sink.ok()) { std::fprintf(stderr, "cannot open the disk for writing (administrator?)\n"); return 1; }
    auto res = isFormat ? applyFormat(fplan, typed, sink, src) : applyPartOp(plan, typed, sink, src, t.sectorSize);
    sink.refreshLayout();
    if (!res.ok) { std::printf("RESULT: FAILED: %s%s\n", res.error.c_str(), res.written ? " (the table may be partially written)" : ""); return 1; }
    std::printf("RESULT: OK (written, flushed and verified by reading back)\n");
    return 0;
}