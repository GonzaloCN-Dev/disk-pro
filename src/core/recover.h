// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
// Read-only recovery: looks for lost partitions by their file-system boot sectors (NTFS, FAT32, exFAT).
// Never writes. Recreating a table entry from a candidate goes through partops (preview + confirmation).
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "layout.h"
#include "tools.h"

namespace diskpro {

struct LostPartition {
    uint64_t firstLba = 0, sectorCount = 0;
    std::string filesystem;
    bool overlapsExisting = false;   // overlaps a partition that the current table already lists
    uint8_t confidence = 0;          // deterministic structural score, not a probability
    std::string evidence;
};

struct RecoverResult {
    bool ok = false, cancelled = false;
    std::vector<LostPartition> found;
    uint64_t scannedBytes = 0;
    std::string error;
};

RecoverResult findLostPartitions(const ByteSource& disk, uint32_t sectorSize, const std::atomic<bool>& cancel,
                                 const ProgressFn& progress = {});

struct RecoverableFile {
    uint64_t directoryEntryOffset = 0; // byte offset inside the FAT32 volume
    std::string path;                  // recovered 8.3 path; '_' replaces the lost first character
    uint64_t size = 0;
    uint32_t firstCluster = 0;
    uint8_t confidence = 0;            // structural score, not a probability
    std::string evidence;
    std::vector<uint32_t> clusters;    // validated read order
};

struct FileScanResult {
    bool ok = false, cancelled = false;
    std::vector<RecoverableFile> files;
    std::string error;
};

// Scans deleted regular-file entries in FAT32 directories that are still reachable.
// It never writes and deliberately does not claim support for deleted directories or FAT LFN reconstruction.
FileScanResult findDeletedFat32Files(const ByteSource& volume, const std::atomic<bool>& cancel,
                                     const ProgressFn& progress = {});

struct FileRecoverResult {
    bool ok = false, cancelled = false;
    uint64_t bytes = 0;
    std::string sha256, error;
};
using RecoverWriteFn = std::function<bool(const void*, size_t)>;

// Extracts one previously scanned candidate through a caller-owned output callback.
// The candidate geometry is revalidated against the current FAT32 volume before any data is returned.
FileRecoverResult recoverFat32File(const ByteSource& volume, const RecoverableFile& file,
                                   const std::atomic<bool>& cancel, const RecoverWriteFn& write,
                                   const ProgressFn& progress = {});
// Creates `outPath` exclusively, never overwrites, and removes a partial output on any failure.
FileRecoverResult recoverFat32FileToNewFile(const ByteSource& volume, const RecoverableFile& file,
                                            const std::string& outPath, const std::atomic<bool>& cancel,
                                            const ProgressFn& progress = {});

}  // namespace diskpro
