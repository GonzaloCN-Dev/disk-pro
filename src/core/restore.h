// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
// Write engine (partition table restore). Separate from the read-only code: nothing in tools.h can write.
// Policy of this version: only small VIRTUAL disks, never the system disk, typed confirmation, read-back verification.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "layout.h"

namespace diskpro {

constexpr uint64_t kMaxWritableDisk = 4ull << 30;  // this version refuses to write to anything bigger

class ByteSink {
public:
    virtual ~ByteSink() = default;
    virtual bool write(uint64_t offset, const void* buf, size_t len) = 0;
    virtual bool flush() = 0;
};

// Writes into an EXISTING file (used by tests / image files). Never creates or grows the file.
class FileSink final : public ByteSink {
public:
    explicit FileSink(const std::string& path);
    ~FileSink() override;
    bool ok() const { return f_ != nullptr; }
    bool write(uint64_t offset, const void* buf, size_t len) override;
    bool flush() override;
private:
    void* f_ = nullptr;
    uint64_t size_ = 0;
};

struct BackupData {
    bool ok = false;
    std::string error, scheme, diskId, sha256;
    uint32_t sectorSize = 512;
    uint64_t diskSize = 0;
    std::vector<uint8_t> head, tail;
};
BackupData loadPartitionBackup(const std::string& path);

struct TargetFacts {
    uint32_t number = 0;
    uint64_t size = 0;
    uint32_t sectorSize = 512;
    bool isSystemDisk = false;
    bool isVirtual = false;
    std::string currentScheme, currentDiskId;
};

struct WriteRange { uint64_t offset = 0, length = 0; };

struct RestorePlan {
    bool allowed = false;
    std::vector<std::string> blockers;  // any entry forbids the operation
    std::vector<std::string> warnings;
    std::vector<WriteRange> ranges;
    std::string token;                  // the exact text the user must type
};
RestorePlan planRestore(const BackupData& b, const TargetFacts& t);

struct RestoreResult {
    bool ok = false;
    bool written = false;   // true if at least one write was attempted
    std::string error;
};
// Writes the backup areas, flushes, then reads back and compares. Re-checks plan, token and ranges itself.
RestoreResult applyRestore(const BackupData& b, const RestorePlan& plan, const std::string& typedToken, ByteSink& sink,
                           const ByteSource& readback);

}  // namespace diskpro
