// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
// Portable, read-only partition table analysis (MBR/GPT). No platform APIs here.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace diskpro {

class ByteSource {
public:
    virtual ~ByteSource() = default;
    virtual uint64_t size() const = 0;
    // Returns false on I/O error or out-of-range read. Never throws.
    virtual bool read(uint64_t offset, void* buf, size_t len) const = 0;
    // Optional file identity; used to prevent recovery outputs from replacing their source image.
    virtual std::string backingFilePath() const { return {}; }
    // Optional physical disk identity for raw-device-backed sources.
    virtual int backingDiskNumber() const { return -1; }
};

class MemorySource final : public ByteSource {
public:
    explicit MemorySource(std::vector<uint8_t> d) : data_(std::move(d)) {}
    uint64_t size() const override { return data_.size(); }
    bool read(uint64_t off, void* buf, size_t len) const override;
    std::vector<uint8_t>& bytes() { return data_; }
private:
    std::vector<uint8_t> data_;
};

// A read-only window [offset, offset+length) over another source: lets every read-only tool work on one partition.
class RangeSource final : public ByteSource {
public:
    RangeSource(std::shared_ptr<const ByteSource> base, uint64_t offset, uint64_t length)
        : base_(std::move(base)), off_(offset), len_(length) {
        const uint64_t bs = base_ ? base_->size() : 0;
        if (off_ > bs) { off_ = bs; len_ = 0; }
        else if (len_ > bs - off_) len_ = bs - off_;
    }
    uint64_t size() const override { return len_; }
    bool read(uint64_t off, void* buf, size_t len) const override {
        if (!base_ || off > len_ || len > len_ - off) return false;
        return base_->read(off_ + off, buf, len);
    }
    std::string backingFilePath() const override { return base_ ? base_->backingFilePath() : std::string{}; }
    int backingDiskNumber() const override { return base_ ? base_->backingDiskNumber() : -1; }
private:
    std::shared_ptr<const ByteSource> base_;
    uint64_t off_ = 0, len_ = 0;
};
class FileSource final : public ByteSource {
public:
    explicit FileSource(const std::string& path);
    ~FileSource() override;
    bool ok() const { return f_ != nullptr; }
    uint64_t size() const override { return size_; }
    bool read(uint64_t off, void* buf, size_t len) const override;
    std::string backingFilePath() const override { return path_; }
private:
    void* f_ = nullptr;
    uint64_t size_ = 0;
    std::string path_;
};

enum class Scheme { Unknown, Raw, MBR, GPT };
enum class Severity { Info, Warning, Error };

struct Diagnostic {
    Severity severity;
    std::string message;
};

struct Partition {
    int index = 0;
    uint64_t firstLba = 0;
    uint64_t sectorCount = 0;
    std::string typeId;      // MBR hex type (e.g. "0x07") or GPT GUID
    std::string typeName;    // human readable when known
    std::string uniqueId;    // GPT partition GUID
    std::string name;        // GPT name
    std::string filesystem;  // detected: NTFS, FAT12/16/32, exFAT, or ""
    bool bootable = false;   // MBR active flag
    bool logical = false;    // MBR logical partition (EBR)
    uint64_t bytes(uint32_t sectorSize) const { return sectorCount * sectorSize; }
};

struct DiskLayout {
    Scheme scheme = Scheme::Unknown;
    uint32_t sectorSize = 512;
    uint64_t totalSectors = 0;
    std::string diskId;  // MBR signature or GPT disk GUID
    std::vector<Partition> partitions;
    std::vector<Diagnostic> diagnostics;
    bool hasErrors() const;
};

// Analyze a disk image/device. sectorSize==0 tries to auto-detect (512, then 4096).
DiskLayout analyze(const ByteSource& src, uint32_t sectorSize = 0);

const char* schemeName(Scheme s);
uint32_t crc32(const void* data, size_t len);
std::string formatGuid(const uint8_t* mixedEndian16);
std::string humanSize(uint64_t bytes);

}  // namespace diskpro
