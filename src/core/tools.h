// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
// Read-only utilities over a ByteSource: SHA-256, imaging, surface scan, hex dump.
#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include "layout.h"

namespace diskpro {

class Sha256 {
public:
    Sha256();
    void update(const void* data, size_t len);
    std::string finalHex();
private:
    void block(const uint8_t* p);
    uint32_t h_[8];
    uint8_t buf_[64];
    size_t n_ = 0;
    uint64_t total_ = 0;
};

struct Progress { uint64_t done = 0, total = 0; uint64_t badBlocks = 0; };
using ProgressFn = std::function<void(const Progress&)>;

struct ImageResult {
    bool ok = false;
    bool cancelled = false;
    uint64_t bytes = 0;
    uint64_t badBlocks = 0;   // blocks that could not be read (written as zeros)
    std::string sha256;       // hash of everything written (bad blocks as zeros)
    std::string error;
};

// Copies [0,size) of src into a new file. Refuses to overwrite an existing file.
ImageResult createImage(const ByteSource& src, const std::string& outPath, const std::atomic<bool>& cancel,
                        const ProgressFn& progress = {}, size_t block = 1 << 20);

// Hash of a source (read-only). Unreadable blocks are counted and hashed as zeros.
ImageResult hashSource(const ByteSource& src, const std::atomic<bool>& cancel, const ProgressFn& progress = {},
                       size_t block = 1 << 20);

struct ScanResult {
    bool ok = false;
    bool cancelled = false;
    uint64_t blocks = 0, badBlocks = 0;
    std::vector<uint64_t> badOffsets;  // first 1000 only
    double mbPerSec = 0;
};
// Read-only surface scan: tries to read every block, never writes.
ScanResult surfaceScan(const ByteSource& src, const std::atomic<bool>& cancel, const ProgressFn& progress = {},
                       size_t block = 1 << 20);

// "00000000  xx xx ...  |ascii|" lines; returns empty string on read error.
std::string hexDump(const ByteSource& src, uint64_t offset, size_t len);

}  // namespace diskpro
