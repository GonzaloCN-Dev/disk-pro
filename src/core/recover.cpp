// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
#include "recover.h"
#include <algorithm>
#include <cstring>

namespace diskpro {

namespace {
uint32_t r32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint64_t r64(const uint8_t* p) { return uint64_t(r32(p)) | uint64_t(r32(p + 4)) << 32; }
bool pow2(uint32_t v) { return v && !(v & (v - 1)); }

// Returns true and fills fs/sectors if `b` (one sector read at `lba`) is a plausible boot sector of a volume that starts there.
bool probe(const uint8_t* b, uint32_t ss, uint64_t lba, std::string& fs, uint64_t& sectors) {
    if (b[510] != 0x55 || b[511] != 0xAA) return false;
    if (!std::memcmp(b + 3, "EXFAT   ", 8)) {
        uint64_t off = r64(b + 64), len = r64(b + 72);
        uint32_t shift = b[108];
        if (shift < 9 || shift > 12 || (1u << shift) != ss || len < 2048) return false;
        if (off != 0 && off != lba) return false;           // a backup copy carries the original offset
        fs = "exFAT"; sectors = len; return true;
    }
    const uint32_t bps = b[11] | uint32_t(b[12]) << 8, spc = b[13];
    if (bps != ss || !pow2(spc) || spc > 128) return false;
    const uint32_t hidden = r32(b + 28);
    if (hidden != 0 && hidden != lba) return false;
    if (!std::memcmp(b + 3, "NTFS    ", 8)) {
        uint64_t total = r64(b + 40);
        if (total < 2048 || b[16] != 0 || b[17] != 0) return false;
        fs = "NTFS"; sectors = total + 1; return true;      // the backup boot sector sits just after the counted sectors
    }
    if (!std::memcmp(b + 82, "FAT32   ", 8)) {
        uint32_t reserved = b[14] | uint32_t(b[15]) << 8, fats = b[16], total = r32(b + 32), fatSz = r32(b + 36);
        if (reserved == 0 || fats < 1 || fats > 2 || total < 2048 || fatSz == 0) return false;
        fs = "FAT32"; sectors = total; return true;
    }
    return false;
}
}  // namespace

RecoverResult findLostPartitions(const ByteSource& disk, uint32_t ss, const std::atomic<bool>& cancel, const ProgressFn& progress) {
    RecoverResult r;
    if (ss != 512 && ss != 4096) { r.error = "unsupported sector size"; return r; }
    const uint64_t size = disk.size() - disk.size() % ss;
    if (size == 0) { r.error = "empty source"; return r; }
    DiskLayout cur = analyze(disk, ss);
    const size_t block = 1u << 20;
    std::vector<uint8_t> buf(block);
    std::vector<LostPartition> cand;
    Progress pr; pr.total = size;
    for (uint64_t off = 0; off < size; off += block) {
        if (cancel.load()) { r.cancelled = true; return r; }
        size_t n = size_t(std::min<uint64_t>(block, size - off));
        if (!disk.read(off, buf.data(), n)) { ++pr.badBlocks; continue; }   // unreadable block: skipped, counted
        for (size_t o = 0; o + ss <= n; o += ss) {
            // cheap pre-filter on the signature before the full probe
            const uint8_t* b = buf.data() + o;
            if (b[510] != 0x55 || b[511] != 0xAA) continue;
            std::string fs; uint64_t sectors = 0;
            uint64_t lba = (off + o) / ss;
            if (!probe(b, ss, lba, fs, sectors)) continue;
            if (lba + sectors > size / ss) continue;          // would not fit on this disk
            cand.push_back({lba, sectors, fs, false});
        }
        pr.done = off + n;
        if (progress) progress(pr);
    }
    // drop candidates located inside a bigger candidate (internal copies), keep the outermost
    std::sort(cand.begin(), cand.end(), [](auto& a, auto& b) { return a.firstLba != b.firstLba ? a.firstLba < b.firstLba : a.sectorCount > b.sectorCount; });
    uint64_t end = 0;
    for (auto& c : cand) {
        if (!r.found.empty() && c.firstLba < end) continue;
        for (auto& p : cur.partitions)
            if (c.firstLba < p.firstLba + p.sectorCount && p.firstLba < c.firstLba + c.sectorCount) c.overlapsExisting = true;
        r.found.push_back(c);
        end = c.firstLba + c.sectorCount;
    }
    r.scannedBytes = size;
    r.ok = true;
    return r;
}

}  // namespace diskpro