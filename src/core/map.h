// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
// Portable partition-map model and read-only diagnostics for a set of byte extents.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "layout.h"

namespace diskpro {

struct Extent {
    int index = 0;
    uint64_t offset = 0;
    uint64_t length = 0;
};

struct MapSegment {
    bool free = false;
    int partitionIndex = 0;  // valid when !free
    uint64_t offset = 0;
    uint64_t length = 0;
    double fraction = 0;  // share of the disk, clamped so tiny segments stay visible
};

struct MapResult {
    std::vector<MapSegment> segments;
    std::vector<Diagnostic> diagnostics;
};

// Builds ordered segments (partitions + unallocated gaps) and diagnoses overlap, range and alignment.
// `minFraction` keeps very small segments visible in a bar chart; fractions are re-normalized to sum to 1.
MapResult buildMap(uint64_t diskSize, uint32_t physicalSector, std::vector<Extent> extents, double minFraction = 0.02);

}  // namespace diskpro
