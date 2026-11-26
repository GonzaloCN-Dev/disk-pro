// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
// Write engine: format ONE partition as FAT32 (own implementation, clean-room from the public FAT32 layout).
// Same policy as the other write engines: only small VIRTUAL disks, never the system disk, typed confirmation,
// read-back verification. Writes stay strictly inside the chosen partition. Quick format: metadata only.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "partops.h"

namespace diskpro {

struct FormatOp {
    int index = 0;            // 1-based partition index as listed by analyze()
    std::string fs = "fat32";
    std::string label;        // up to 11 characters, A-Z 0-9 space _ -  (upper-cased)
    uint32_t volumeId = 0;    // random when 0
};

struct FormatPlan {
    bool allowed = false;
    uint64_t targetSize = 0;
    uint32_t sectorSize = 0;
    std::vector<std::string> blockers, warnings;
    std::vector<TableWrite> writes;
    uint64_t partOffset = 0, partLength = 0;
    std::string token;        // "FORMAT DISK N PARTITION I"
    std::string summary;
    uint32_t clusterBytes = 0, clusters = 0;
};

FormatPlan planFormat(const ByteSource& disk, const TargetFacts& t, const FormatOp& op);
PartOpResult applyFormat(const FormatPlan& plan, const std::string& typedToken, ByteSink& sink, const ByteSource& readback);

}  // namespace diskpro
