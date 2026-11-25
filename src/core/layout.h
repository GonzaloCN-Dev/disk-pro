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

class FileSource final : public ByteSource {
public:
    explicit FileSource(const std::string& path);
    ~FileSource() override;
    bool ok() const { return f_ != nullptr; }
    uint64_t size() const override { return size_; }
    bool read(uint64_t off, void* buf, size_t len) const override;
private:
    void* f_ = nullptr;
    uint64_t size_ = 0;
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
