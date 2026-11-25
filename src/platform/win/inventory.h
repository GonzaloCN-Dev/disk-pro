// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
// Windows read-only disk inventory. Opens devices with access 0 (metadata only).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace diskpro::win {

struct PartitionInfo {
    uint32_t number = 0;
    std::string style;  // MBR / GPT
    uint64_t offsetBytes = 0;
    uint64_t lengthBytes = 0;
    std::string typeName;
    std::string volume;      // drive letter root, e.g. "C:\\", when mounted
    std::string filesystem;  // reported by Windows for the mounted volume
};

struct DiskInfo {
    uint32_t number = 0;
    std::string path;
    std::string vendor, product, serialMasked, busType;
    uint64_t sizeBytes = 0;
    uint32_t logicalSector = 0, physicalSector = 0;
    std::string style;  // MBR / GPT / RAW / ""
    std::vector<PartitionInfo> partitions;
    std::vector<std::string> volumes;  // e.g. "C:\\"
    bool isSystemDisk = false;
    bool isVirtual = false;
    std::string error;  // non-empty if some queries failed
};

// Enumerates PhysicalDrive0..N. Never writes. Does not require elevation for the fields it can read.
std::vector<DiskInfo> enumerateDisks();

}  // namespace diskpro::win

