// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
#include "recover.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <unordered_set>

namespace diskpro {

namespace {
uint32_t r32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint64_t r64(const uint8_t* p) { return uint64_t(r32(p)) | uint64_t(r32(p + 4)) << 32; }
uint16_t r16(const uint8_t* p) { return uint16_t(p[0] | uint16_t(p[1]) << 8); }
bool pow2(uint32_t v) { return v && !(v & (v - 1)); }

struct Fat32 {
    uint32_t ss = 0, spc = 0, reserved = 0, fats = 0, fatSectors = 0, root = 0, clusters = 0;
    uint64_t dataOffset = 0, clusterBytes = 0;
    std::vector<uint8_t> fat;
};

bool loadFat32(const ByteSource& volume, Fat32& f, std::string& error) {
    uint8_t b[512]{};
    if (volume.size() < sizeof b || !volume.read(0, b, sizeof b)) { error = "cannot read FAT32 boot sector"; return false; }
    if (std::memcmp(b + 82, "FAT32   ", 8) || b[510] != 0x55 || b[511] != 0xAA) { error = "source is not a FAT32 volume"; return false; }
    f.ss = r16(b + 11); f.spc = b[13]; f.reserved = r16(b + 14); f.fats = b[16];
    f.fatSectors = r32(b + 36); f.root = r32(b + 44);
    const uint64_t total = r32(b + 32) ? r32(b + 32) : r16(b + 19);
    if ((f.ss != 512 && f.ss != 4096) || !pow2(f.spc) || f.spc > 128 || f.reserved == 0 ||
        f.fats == 0 || f.fats > 2 || f.fatSectors == 0 || total <= f.reserved + uint64_t(f.fats) * f.fatSectors) {
        error = "invalid FAT32 geometry"; return false;
    }
    f.clusterBytes = uint64_t(f.ss) * f.spc;
    f.dataOffset = (uint64_t(f.reserved) + uint64_t(f.fats) * f.fatSectors) * f.ss;
    f.clusters = uint32_t((total - f.reserved - uint64_t(f.fats) * f.fatSectors) / f.spc);
    const uint64_t fatBytes = uint64_t(f.fatSectors) * f.ss;
    if (f.clusters < 1 || f.root < 2 || f.root >= f.clusters + 2 || fatBytes > (64ull << 20) ||
        fatBytes < uint64_t(f.clusters + 2) * 4 || total * f.ss > volume.size()) {
        error = "FAT32 geometry lies outside the source"; return false;
    }
    f.fat.resize(size_t(fatBytes));
    if (!volume.read(uint64_t(f.reserved) * f.ss, f.fat.data(), f.fat.size())) { error = "cannot read FAT"; return false; }
    return true;
}

uint32_t fatEntry(const Fat32& f, uint32_t c) {
    if (uint64_t(c) * 4 + 4 > f.fat.size()) return 0x0FFFFFF7;
    return r32(f.fat.data() + size_t(c) * 4) & 0x0FFFFFFF;
}

bool validCluster(const Fat32& f, uint32_t c) { return c >= 2 && c < f.clusters + 2; }
uint64_t clusterOffset(const Fat32& f, uint32_t c) { return f.dataOffset + uint64_t(c - 2) * f.clusterBytes; }

bool chain(const Fat32& f, uint32_t first, uint32_t needed, std::vector<uint32_t>& out) {
    std::unordered_set<uint32_t> seen;
    uint32_t c = first;
    while (out.size() < needed && validCluster(f, c) && seen.insert(c).second) {
        out.push_back(c);
        if (out.size() == needed) return true;
        c = fatEntry(f, c);
    }
    out.clear();
    return false;
}

std::string shortName(const uint8_t* e, bool deleted) {
    std::string base, ext;
    for (int i = 0; i < 8 && e[i] != ' '; ++i) {
        unsigned char c = e[i];
        if (i == 0 && deleted) c = '_';
        base += (c >= 32 && c < 127 && c != '/' && c != '\\' ? char(c) : '_');
    }
    for (int i = 8; i < 11 && e[i] != ' '; ++i) {
        unsigned char c = e[i];
        ext += (c >= 32 && c < 127 && c != '/' && c != '\\' ? char(c) : '_');
    }
    if (base.empty()) base = "_";
    return ext.empty() ? base : base + "." + ext;
}

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
            LostPartition candidate;
            candidate.firstLba = lba;
            candidate.sectorCount = sectors;
            candidate.filesystem = fs;
            candidate.confidence = 70;
            candidate.evidence = "valid " + fs + " boot-sector structure";
            if ((lba * ss) % (1u << 20) == 0) {
                candidate.confidence = uint8_t(candidate.confidence + 10);
                candidate.evidence += "; 1 MiB aligned";
            }
            uint64_t backupLba = 0;
            if (fs == "exFAT" && sectors > 24) backupLba = lba + 12;
            else if (fs == "FAT32") {
                const uint16_t backup = r16(b + 50);
                if (backup && backup < sectors) backupLba = lba + backup;
            }
            if (backupLba) {
                std::vector<uint8_t> backup(ss);
                if (disk.read(backupLba * ss, backup.data(), backup.size()) && std::memcmp(backup.data(), b, ss) == 0) {
                    candidate.confidence = uint8_t(std::min<int>(100, candidate.confidence + 20));
                    candidate.evidence += "; matching backup boot sector";
                }
            }
            cand.push_back(std::move(candidate));
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
        if (c.overlapsExisting && c.confidence >= 10) {
            c.confidence = uint8_t(c.confidence - 10);
            c.evidence += "; overlaps current table";
        }
        r.found.push_back(c);
        end = c.firstLba + c.sectorCount;
    }
    r.scannedBytes = size;
    r.ok = true;
    return r;
}

FileScanResult findDeletedFat32Files(const ByteSource& volume, const std::atomic<bool>& cancel,
                                     const ProgressFn& progress) {
    FileScanResult result;
    Fat32 f;
    if (!loadFat32(volume, f, result.error)) return result;
    std::unordered_set<uint32_t> visitedDirs;
    std::unordered_set<uint32_t> liveClusters;
    struct Pending { uint32_t cluster; std::string path; };
    std::vector<Pending> pending{{f.root, ""}};
    Progress pr; pr.total = volume.size();
    std::vector<uint8_t> data(size_t(f.clusterBytes));
    while (!pending.empty()) {
        if (cancel.load()) { result.cancelled = true; return result; }
        Pending dir = std::move(pending.back());
        pending.pop_back();
        if (!visitedDirs.insert(dir.cluster).second || visitedDirs.size() > 100000) continue;
        std::vector<uint32_t> dirChain;
        std::unordered_set<uint32_t> seen;
        uint32_t dc = dir.cluster;
        while (dc >= 2 && dc < f.clusters + 2 && seen.insert(dc).second) {
            dirChain.push_back(dc);
            liveClusters.insert(dc);
            const uint32_t next = fatEntry(f, dc);
            if (next >= 0x0FFFFFF8) break;
            dc = next;
        }
        for (uint32_t dirCluster : dirChain) {
            const uint64_t clusterOffset = f.dataOffset + uint64_t(dirCluster - 2) * f.clusterBytes;
            if (!volume.read(clusterOffset, data.data(), data.size())) continue;
            for (size_t off = 0; off + 32 <= data.size(); off += 32) {
                const uint8_t* e = data.data() + off;
                if (e[0] == 0x00) break;
                const bool deleted = e[0] == 0xE5;
                const uint8_t attr = e[11];
                if (attr == 0x0F || (attr & 0x08)) continue;
                const uint32_t first = uint32_t(r16(e + 20)) << 16 | r16(e + 26);
                const uint32_t bytes = r32(e + 28);
                const std::string name = shortName(e, deleted);
                if (!deleted && (attr & 0x10) && name != "." && name != ".." && first >= 2 && first < f.clusters + 2) {
                    pending.push_back({first, dir.path + name + "/"});
                    continue;
                }
                if (!deleted && !(attr & 0x10) && bytes && first >= 2 && first < f.clusters + 2) {
                    const uint64_t count64 = (uint64_t(bytes) + f.clusterBytes - 1) / f.clusterBytes;
                    if (count64 <= f.clusters) {
                        std::vector<uint32_t> active;
                        if (chain(f, first, uint32_t(count64), active))
                            liveClusters.insert(active.begin(), active.end());
                    }
                    continue;
                }
                if (!deleted || (attr & 0x10) || bytes == 0 || first < 2 || first >= f.clusters + 2) continue;
                const uint64_t needed64 = (uint64_t(bytes) + f.clusterBytes - 1) / f.clusterBytes;
                if (needed64 == 0 || needed64 > f.clusters || uint64_t(first) + needed64 > uint64_t(f.clusters) + 2) continue;
                RecoverableFile file;
                file.directoryEntryOffset = clusterOffset + off;
                file.path = dir.path + name;
                file.size = bytes;
                file.firstCluster = first;
                const uint32_t needed = uint32_t(needed64);
                if (chain(f, first, needed, file.clusters)) {
                    file.confidence = 85;
                    file.evidence = "deleted FAT32 entry; intact FAT chain";
                } else {
                    bool free = true;
                    for (uint32_t i = 0; i < needed; ++i)
                        if (fatEntry(f, first + i) != 0) { free = false; break; }
                    if (!free) continue;
                    file.clusters.reserve(needed);
                    for (uint32_t i = 0; i < needed; ++i) file.clusters.push_back(first + i);
                    file.confidence = 65;
                    file.evidence = "deleted FAT32 entry; contiguous clusters still marked free";
                }
                result.files.push_back(std::move(file));
            }
            pr.done = std::min<uint64_t>(volume.size(), clusterOffset + f.clusterBytes);
            if (progress) progress(pr);
        }
    }
    std::erase_if(result.files, [&](const RecoverableFile& file) {
        return std::any_of(file.clusters.begin(), file.clusters.end(), [&](uint32_t c) { return liveClusters.contains(c); });
    });
    for (auto& file : result.files) file.evidence += "; not referenced by a reachable live entry";
    std::sort(result.files.begin(), result.files.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
    result.ok = true;
    return result;
}

FileRecoverResult recoverFat32File(const ByteSource& volume, const RecoverableFile& file,
                                   const std::atomic<bool>& cancel, const RecoverWriteFn& write,
                                   const ProgressFn& progress) {
    FileRecoverResult result;
    if (!write) { result.error = "output callback is missing"; return result; }
    Fat32 f;
    if (!loadFat32(volume, f, result.error)) return result;
    uint8_t e[32]{};
    if (file.directoryEntryOffset > volume.size() || sizeof e > volume.size() - file.directoryEntryOffset ||
        !volume.read(file.directoryEntryOffset, e, sizeof e)) {
        result.error = "directory entry is no longer readable"; return result;
    }
    const uint32_t first = uint32_t(r16(e + 20)) << 16 | r16(e + 26);
    const uint32_t bytes = r32(e + 28);
    if (e[0] != 0xE5 || (e[11] & 0x18) || first != file.firstCluster || bytes != file.size) {
        result.error = "candidate changed since scan"; return result;
    }
    const uint64_t needed64 = (uint64_t(bytes) + f.clusterBytes - 1) / f.clusterBytes;
    if (!needed64 || needed64 > f.clusters || uint64_t(first) + needed64 > uint64_t(f.clusters) + 2) {
        result.error = "candidate geometry is invalid"; return result;
    }
    std::vector<uint32_t> clusters;
    const uint32_t needed = uint32_t(needed64);
    if (!chain(f, first, needed, clusters)) {
        for (uint32_t i = 0; i < needed; ++i)
            if (fatEntry(f, first + i) != 0) { result.error = "candidate clusters have been reused"; return result; }
        for (uint32_t i = 0; i < needed; ++i) clusters.push_back(first + i);
    }
    if (clusters != file.clusters) { result.error = "candidate allocation changed since scan"; return result; }
    std::vector<uint8_t> data(size_t(f.clusterBytes));
    Sha256 hash;
    Progress pr; pr.total = bytes;
    uint64_t remaining = bytes;
    for (uint32_t c : clusters) {
        if (cancel.load()) { result.cancelled = true; result.error = "cancelled"; return result; }
        const uint64_t offset = f.dataOffset + uint64_t(c - 2) * f.clusterBytes;
        const size_t n = size_t(std::min<uint64_t>(remaining, f.clusterBytes));
        if (offset > volume.size() || n > volume.size() - offset || !volume.read(offset, data.data(), n)) {
            result.error = "read error while recovering file"; return result;
        }
        if (!write(data.data(), n)) { result.error = "output write failed"; return result; }
        hash.update(data.data(), n);
        remaining -= n;
        result.bytes += n;
        pr.done = result.bytes;
        if (progress) progress(pr);
    }
    if (remaining) { result.error = "incomplete cluster chain"; return result; }
    result.sha256 = hash.finalHex();
    result.ok = true;
    return result;
}

FileRecoverResult recoverFat32FileToNewFile(const ByteSource& volume, const RecoverableFile& file,
                                            const std::string& outPath, const std::atomic<bool>& cancel,
                                            const ProgressFn& progress) {
    namespace fs = std::filesystem;
    FileRecoverResult result;
    std::FILE* out = nullptr;
#ifdef _WIN32
    out = _wfopen(fs::u8path(outPath).c_str(), L"wbx");
#else
    out = std::fopen(outPath.c_str(), "wbx");
#endif
    if (!out) { result.error = "cannot create output file (it may already exist)"; return result; }
    result = recoverFat32File(volume, file, cancel, [out](const void* p, size_t n) {
        return std::fwrite(p, 1, n, out) == n;
    }, progress);
    if (result.ok && std::fflush(out) != 0) { result.ok = false; result.error = "cannot flush output"; }
    if (std::fclose(out) != 0 && result.ok) { result.ok = false; result.error = "cannot close output"; }
    if (!result.ok) {
        std::error_code ec;
        fs::remove(fs::u8path(outPath), ec);
    }
    return result;
}

}  // namespace diskpro
