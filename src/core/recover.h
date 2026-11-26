// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
// Read-only recovery: looks for lost partitions by their file-system boot sectors (NTFS, FAT32, exFAT).
// Never writes. Recreating a table entry from a candidate goes through partops (preview + confirmation).
#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include "layout.h"
#include "tools.h"

namespace diskpro {

struct LostPartition {
    uint64_t firstLba = 0, sectorCount = 0;
    std::string filesystem;
    bool overlapsExisting = false;   // overlaps a partition that the current table already lists
};

struct RecoverResult {
    bool ok = false, cancelled = false;
    std::vector<LostPartition> found;
    uint64_t scannedBytes = 0;
    std::string error;
};

RecoverResult findLostPartitions(const ByteSource& disk, uint32_t sectorSize, const std::atomic<bool>& cancel,
                                 const ProgressFn& progress = {});

}  // namespace diskpro