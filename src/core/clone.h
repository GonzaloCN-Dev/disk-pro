// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
// Write engine: clone a disk or image onto a VIRTUAL disk. Same policy as restore.h: never the system disk, <= 4 GiB,
// typed confirmation, verification by reading back. Portable: works on ByteSource/ByteSink only.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "restore.h"

namespace diskpro {

struct CloneSourceFacts {
    uint64_t size = 0;
    uint32_t sectorSize = 512;
    bool isTargetItself = false;   // true if the source is the same disk as the target
    std::string label;
};

struct ClonePlan {
    bool allowed = false;
    std::vector<std::string> blockers;
    std::vector<std::string> warnings;
    uint64_t bytes = 0;            // bytes that will be copied, from offset 0
    std::string token;             // "CLONE TO DISK N"
};
ClonePlan planClone(const CloneSourceFacts& s, const TargetFacts& t);

// phase 0 = copying, 1 = verifying. Return false to cancel.
using CloneProgress = std::function<bool(int phase, uint64_t done, uint64_t total)>;

struct CloneResult {
    bool ok = false, written = false, cancelled = false;
    uint64_t copied = 0;
    std::string error;
};
// Copies [0, plan.bytes) in 1 MiB blocks, flushes, then reads both sides back and compares.
CloneResult applyClone(const ByteSource& src, ByteSink& dst, const ByteSource& dstReadback, const ClonePlan& plan,
                       const std::string& typedToken, const CloneProgress& progress, bool verify = true);

}  // namespace diskpro