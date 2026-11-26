// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
#include "report.h"
#include <cstdio>

namespace diskpro::win {
namespace {
std::string size(uint64_t b) {
    char s[48];
    if (b >= (1ull << 30)) std::snprintf(s, sizeof s, "%.2f GiB", double(b) / double(1ull << 30));
    else std::snprintf(s, sizeof s, "%.2f MiB", double(b) / double(1ull << 20));
    return s;
}
}  // namespace

std::string buildReport(const std::vector<DiskInfo>& disks, const std::vector<std::string>& health, const std::string& v) {
    std::string o = "Disk Pro " + v + " - disk report (read-only)\r\n";
    o += "The serial number is masked. Nothing in this file was read from disk contents.\r\n\r\n";
    for (size_t i = 0; i < disks.size(); ++i) {
        const auto& d = disks[i];
        o += "Disk " + std::to_string(d.number) + ": " + d.vendor + " " + d.product + (d.isSystemDisk ? "  [SYSTEM]" : "") + "\r\n";
        o += "  Size: " + size(d.sizeBytes) + "   Bus: " + d.busType + "   Style: " + (d.style.empty() ? "?" : d.style) +
             "   Sector: " + std::to_string(d.logicalSector) + "/" + std::to_string(d.physicalSector) + (d.isVirtual ? "   Virtual" : "") + "\r\n";
        if (!d.serialMasked.empty()) o += "  Serial: " + d.serialMasked + "\r\n";
        if (i < health.size() && !health[i].empty()) o += "  Health: " + health[i] + "\r\n";
        if (!d.error.empty()) o += "  Notes: " + d.error + "\r\n";
        for (const auto& p : d.partitions) {
            o += "  Partition " + std::to_string(p.number) + ": offset " + std::to_string(p.offsetBytes) + ", " + size(p.lengthBytes) + ", " + p.typeName;
            if (!p.filesystem.empty()) o += ", " + p.filesystem;
            if (!p.volume.empty()) o += ", " + p.volume;
            o += "\r\n";
        }
        o += "\r\n";
    }
    if (disks.empty()) o += "No disks found.\r\n";
    return o;
}

}  // namespace diskpro::win
