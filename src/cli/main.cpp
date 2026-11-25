// Disk Pro CLI - MIT License - Copyright (c) Gonzalo Cruz Núñez
// Read-only: analyzes a disk image file. Never writes.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "layout.h"

using namespace diskpro;

int main(int argc, char** argv) {
    if (argc < 2 || !std::strcmp(argv[1], "--help")) {
        std::puts("Usage: diskpro-cli <image-file> [sector-size 512|4096]\nRead-only analysis of MBR/GPT layout.");
        return argc < 2 ? 2 : 0;
    }
    uint32_t ss = 0;
    if (argc > 2) {
        ss = uint32_t(std::strtoul(argv[2], nullptr, 10));
        if (ss != 512 && ss != 4096) { std::fputs("Invalid sector size\n", stderr); return 2; }
    }
    FileSource src(argv[1]);
    if (!src.ok()) { std::fputs("Cannot open file for reading\n", stderr); return 2; }
    DiskLayout l = analyze(src, ss);
    std::printf("Scheme: %s  Id: %s  Sector: %u  Size: %s\n", schemeName(l.scheme), l.diskId.c_str(), l.sectorSize,
                humanSize(l.totalSectors * l.sectorSize).c_str());
    std::printf("%-3s %-12s %-12s %-10s %-16s %-8s %s\n", "#", "FirstLBA", "Sectors", "Size", "Type", "FS", "Name");
    for (auto& p : l.partitions)
        std::printf("%-3d %-12llu %-12llu %-10s %-16s %-8s %s%s\n", p.index, (unsigned long long)p.firstLba,
                    (unsigned long long)p.sectorCount, humanSize(p.bytes(l.sectorSize)).c_str(),
                    p.typeName.empty() ? p.typeId.c_str() : p.typeName.c_str(), p.filesystem.c_str(), p.name.c_str(),
                    p.bootable ? " [active]" : "");
    for (auto& d : l.diagnostics)
        std::printf("[%s] %s\n", d.severity == Severity::Error ? "ERROR" : d.severity == Severity::Warning ? "WARN" : "INFO", d.message.c_str());
    return l.hasErrors() ? 1 : 0;
}
