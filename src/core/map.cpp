// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
#include "map.h"

#include <algorithm>

namespace diskpro {

MapResult buildMap(uint64_t diskSize, uint32_t physicalSector, std::vector<Extent> ex, double minFraction) {
    MapResult r;
    if (diskSize == 0) { r.diagnostics.push_back({Severity::Error, "Disk size unknown"}); return r; }
    std::sort(ex.begin(), ex.end(), [](auto& a, auto& b) { return a.offset < b.offset; });

    const uint64_t kMiB = 1024 * 1024;
    uint64_t cursor = 0;
    const uint64_t gapThreshold = kMiB;  // gaps up to 1 MiB are alignment padding, not reported as free space
    for (auto& e : ex) {
        if (e.length == 0 || e.offset > diskSize || e.length > diskSize - e.offset) {
            r.diagnostics.push_back({Severity::Error, "Partition " + std::to_string(e.index) + " lies outside the disk"});
            continue;
        }
        if (e.offset < cursor) {
            r.diagnostics.push_back({Severity::Error, "Partition " + std::to_string(e.index) + " overlaps the previous one"});
        } else if (e.offset - cursor > gapThreshold) {
            r.segments.push_back({true, 0, cursor, e.offset - cursor, 0});
        }
        if (physicalSector && (e.offset % physicalSector))
            r.diagnostics.push_back({Severity::Warning, "Partition " + std::to_string(e.index) + " is not aligned to the physical sector size"});
        r.segments.push_back({false, e.index, e.offset, e.length, 0});
        cursor = std::max(cursor, e.offset + e.length);
    }
    if (diskSize > cursor && diskSize - cursor > gapThreshold) r.segments.push_back({true, 0, cursor, diskSize - cursor, 0});

    double sum = 0;
    for (auto& s : r.segments) { s.fraction = std::max(double(s.length) / double(diskSize), minFraction); sum += s.fraction; }
    for (auto& s : r.segments) s.fraction /= sum ? sum : 1;
    return r;
}

}  // namespace diskpro

