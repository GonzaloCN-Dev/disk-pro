// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
// Write engine: partition TABLE operations (MBR/GPT). Same policy as restore.h: only small VIRTUAL disks, never the
// system disk, typed confirmation, read-back verification. Only the table areas are ever written; file systems and
// partition contents are never touched. Primary partitions only (extended/logical MBR layouts are refused).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "restore.h"

namespace diskpro {

struct PartOp {
    enum class Kind { InitMbr, InitGpt, Create, Delete, SetType, SetActive, Resize, Merge };
    Kind kind = Kind::Create;
    int index = 0;             // 1-based, as listed by analyze(); Merge: first partition
    int index2 = 0;            // Merge: second partition (must be adjacent)
    uint64_t firstLba = 0;     // Create / Resize
    uint64_t sectorCount = 0;  // Create / Resize
    std::string type;          // alias (ntfs, fat32, efi, linux, swap), "0x07", or a GPT GUID
    std::string name;          // GPT name (Create)
    std::string uniqueId;      // optional GUID (GPT Create / InitGpt); random when empty
    uint32_t mbrSignature = 0; // optional (InitMbr); random when 0
};

struct TableWrite { uint64_t offset = 0; std::vector<uint8_t> data; };

struct PartOpPlan {
    bool allowed = false;
    std::vector<std::string> blockers;
    std::vector<std::string> warnings;
    std::vector<TableWrite> writes;
    std::string token;          // "EDIT DISK N"
    std::string summary;
};

// Reads the current table from `disk` and computes the exact bytes to write. Never writes.
PartOpPlan planPartOp(const ByteSource& disk, const TargetFacts& t, const PartOp& op);

struct PartOpResult {
    bool ok = false;
    bool written = false;
    std::string error;
};
// Re-checks plan and token, writes, flushes, reads back, compares and re-analyzes the result (must have no errors).
PartOpResult applyPartOp(const PartOpPlan& plan, const std::string& typedToken, ByteSink& sink, const ByteSource& readback,
                         uint32_t sectorSize);

// Exposed for tests.
bool parseGuid(const std::string& text, uint8_t out[16]);
uint64_t alignSectors(uint32_t sectorSize);   // 1 MiB expressed in sectors

}  // namespace diskpro
