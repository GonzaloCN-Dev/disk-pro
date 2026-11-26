// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
// Read-only inventory of physical disks (metadata only).
#include <cstdio>

#include "inventory.h"
#include "smart.h"

int main() {
    auto disks = diskpro::win::enumerateDisks();
    if (disks.empty()) std::puts("No disks found.");
    for (auto& d : disks) {
        std::printf("Disk %u  %s %s  [%s]  %.2f GiB  %s  sector %u/%u  serial %s%s%s\n", d.number, d.vendor.c_str(),
                    d.product.c_str(), d.busType.c_str(), double(d.sizeBytes) / (1024.0 * 1024 * 1024), d.style.c_str(),
                    d.logicalSector, d.physicalSector, d.serialMasked.c_str(), d.isSystemDisk ? "  [SYSTEM]" : "",
                    d.isVirtual ? "  [virtual]" : "");
        for (auto& v : d.volumes) std::printf("   volume %s\n", v.c_str());
        for (auto& p : d.partitions)
            std::printf("   #%u  offset %llu  length %.2f MiB  %s  %s %s\n", p.number, (unsigned long long)p.offsetBytes,
                        double(p.lengthBytes) / (1024.0 * 1024), p.typeName.c_str(), p.volume.c_str(), p.filesystem.c_str());
        {
            auto h = diskpro::win::queryHealth(d.number);
            if (h.nvmeAvailable)
                std::printf("   health: NVMe warn=0x%02X temp=%dC spare=%u%% used=%u%% poh=%llu unsafe=%llu mediaErr=%llu\n", h.criticalWarning,
                            h.temperatureC, h.availableSparePct, h.percentageUsed, (unsigned long long)h.powerOnHours,
                            (unsigned long long)h.unsafeShutdowns, (unsigned long long)h.mediaErrors);
            if (h.ataAvailable)
                std::printf("   health: ATA SMART temp=%dC poh=%llu reallocated=%llu pending=%llu uncorrectable=%llu\n",
                            h.temperatureC, (unsigned long long)h.powerOnHours, (unsigned long long)h.reallocatedSectors,
                            (unsigned long long)h.pendingSectors, (unsigned long long)h.uncorrectableSectors);
            if (h.predictFailureAvailable) std::printf("   predict-failure: %s\n", h.predictFailure ? "YES" : "no");
            if (!h.note.empty()) std::printf("   health: %s\n", h.note.c_str());
        }
        if (!d.error.empty()) std::printf("   (partial: %s)\n", d.error.c_str());
    }
    return 0;
}


