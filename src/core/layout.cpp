// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
#include "layout.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace diskpro {

namespace {

uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint64_t le64(const uint8_t* p) { return uint64_t(le32(p)) | uint64_t(le32(p + 4)) << 32; }

void diag(DiskLayout& l, Severity s, std::string m) { l.diagnostics.push_back({s, std::move(m)}); }

const char* mbrTypeName(uint8_t t) {
    switch (t) {
        case 0x01: return "FAT12";
        case 0x04: case 0x06: case 0x0E: return "FAT16";
        case 0x05: case 0x0F: case 0x85: return "Extended";
        case 0x07: return "NTFS/exFAT/HPFS";
        case 0x0B: case 0x0C: return "FAT32";
        case 0x27: return "Windows RE";
        case 0x82: return "Linux swap";
        case 0x83: return "Linux";
        case 0xEE: return "GPT protective";
        case 0xEF: return "EFI System";
        default: return "";
    }
}

bool isExtended(uint8_t t) { return t == 0x05 || t == 0x0F || t == 0x85; }

const char* gptTypeName(const std::string& g) {
    struct E { const char* guid; const char* name; };
    static const E table[] = {
        {"C12A7328-F81F-11D2-BA4B-00A0C93EC93B", "EFI System"},
        {"E3C9E316-0B5C-4DB8-817D-F92DF00215AE", "Microsoft Reserved"},
        {"EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", "Basic data"},
        {"DE94BBA4-06D1-4D40-A16A-BFD50179D6AC", "Windows RE"},
        {"0FC63DAF-8483-4772-8E79-3D69D8477DE4", "Linux filesystem"},
        {"0657FD6D-A4AB-43C4-84E5-0933C84B4F4F", "Linux swap"},
    };
    for (auto& e : table) if (g == e.guid) return e.name;
    return "";
}

std::string utf16Name(const uint8_t* p, size_t units) {
    std::string out;  // BMP -> UTF-8; surrogates replaced with '?'
    for (size_t i = 0; i < units; ++i) {
        uint32_t c = le16(p + i * 2);
        if (c == 0) break;
        if (c >= 0xD800 && c <= 0xDFFF) c = '?';
        if (c < 0x80) out += char(c);
        else if (c < 0x800) { out += char(0xC0 | (c >> 6)); out += char(0x80 | (c & 0x3F)); }
        else { out += char(0xE0 | (c >> 12)); out += char(0x80 | ((c >> 6) & 0x3F)); out += char(0x80 | (c & 0x3F)); }
    }
    return out;
}

std::string detectFilesystem(const ByteSource& src, uint32_t ss, const Partition& p) {
    std::array<uint8_t, 512> b{};
    uint64_t off = p.firstLba * ss;
    if (off + 512 > src.size() || !src.read(off, b.data(), b.size())) return "";
    if (b[510] != 0x55 || b[511] != 0xAA) return "";
    if (!std::memcmp(&b[3], "NTFS    ", 8)) return "NTFS";
    if (!std::memcmp(&b[3], "EXFAT   ", 8)) return "exFAT";
    if (!std::memcmp(&b[82], "FAT32   ", 8)) return "FAT32";
    if (!std::memcmp(&b[54], "FAT12   ", 8)) return "FAT12";
    if (!std::memcmp(&b[54], "FAT16   ", 8)) return "FAT16";
    return "";
}

void validatePartitions(DiskLayout& l, uint64_t firstUsable, uint64_t lastUsable) {
    for (auto& p : l.partitions) {
        if (p.sectorCount == 0)
            diag(l, Severity::Error, "Partition " + std::to_string(p.index) + " has zero length");
        else if (p.firstLba < firstUsable || p.firstLba + p.sectorCount - 1 > lastUsable)
            diag(l, Severity::Error, "Partition " + std::to_string(p.index) + " lies outside the usable area (or the image is truncated)");
    }
    for (size_t i = 0; i < l.partitions.size(); ++i)
        for (size_t j = i + 1; j < l.partitions.size(); ++j) {
            auto& a = l.partitions[i]; auto& b = l.partitions[j];
            if (a.sectorCount && b.sectorCount && a.firstLba < b.firstLba + b.sectorCount && b.firstLba < a.firstLba + a.sectorCount)
                diag(l, Severity::Error, "Partitions " + std::to_string(a.index) + " and " + std::to_string(b.index) + " overlap");
        }
}

bool parseGpt(const ByteSource& src, DiskLayout& l) {
    const uint32_t ss = l.sectorSize;
    std::vector<uint8_t> h(ss);
    if (!src.read(uint64_t(ss), h.data(), ss) || std::memcmp(h.data(), "EFI PART", 8) != 0) return false;

    l.scheme = Scheme::GPT;
    uint32_t hsz = le32(&h[12]);
    if (hsz < 92 || hsz > ss) { diag(l, Severity::Error, "GPT header has invalid size"); return true; }
    uint32_t stored = le32(&h[16]);
    std::vector<uint8_t> hc(h.begin(), h.begin() + hsz);
    std::memset(&hc[16], 0, 4);
    if (crc32(hc.data(), hsz) != stored) diag(l, Severity::Error, "GPT primary header CRC mismatch");

    uint64_t myLba = le64(&h[24]), altLba = le64(&h[32]);
    uint64_t firstUsable = le64(&h[40]), lastUsable = le64(&h[48]);
    l.diskId = formatGuid(&h[56]);
    uint64_t entriesLba = le64(&h[72]);
    uint32_t count = le32(&h[80]), esize = le32(&h[84]);
    uint32_t entriesCrc = le32(&h[88]);

    if (myLba != 1) diag(l, Severity::Warning, "GPT header MyLBA is not 1");
    if (l.totalSectors && altLba != l.totalSectors - 1)
        diag(l, Severity::Warning, "GPT backup header is not at the last sector (disk resized or image truncated)");
    if (l.totalSectors && lastUsable >= l.totalSectors) diag(l, Severity::Error, "GPT LastUsableLBA is beyond the end of the disk");
    if (firstUsable > lastUsable) { diag(l, Severity::Error, "GPT usable range is invalid"); return true; }
    if (esize < 128 || esize > 4096 || (esize & (esize - 1)) || count > 4096) {
        diag(l, Severity::Error, "GPT partition entry geometry is invalid"); return true;
    }

    uint64_t tableBytes = uint64_t(count) * esize;
    std::vector<uint8_t> table(tableBytes);
    if (entriesLba == 0 || !src.read(entriesLba * ss, table.data(), table.size())) {
        diag(l, Severity::Error, "Cannot read GPT partition entry array (I/O error or out of range)"); return true;
    }
    if (crc32(table.data(), table.size()) != entriesCrc) diag(l, Severity::Error, "GPT partition entry array CRC mismatch");

    // Backup header presence/consistency (read-only check)
    if (l.totalSectors > 2) {
        std::vector<uint8_t> bh(ss);
        if (!src.read((l.totalSectors - 1) * ss, bh.data(), ss) || std::memcmp(bh.data(), "EFI PART", 8) != 0)
            diag(l, Severity::Warning, "GPT backup header missing or unreadable");
        else {
            uint32_t bsz = le32(&bh[12]);
            if (bsz >= 92 && bsz <= ss) {
                uint32_t bstored = le32(&bh[16]);
                std::vector<uint8_t> bc(bh.begin(), bh.begin() + bsz);
                std::memset(&bc[16], 0, 4);
                if (crc32(bc.data(), bsz) != bstored) diag(l, Severity::Error, "GPT backup header CRC mismatch");
            }
        }
    }

    static const uint8_t zero[16] = {};
    int idx = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* e = &table[size_t(i) * esize];
        if (!std::memcmp(e, zero, 16)) continue;
        Partition p;
        p.index = ++idx;
        p.typeId = formatGuid(e);
        p.typeName = gptTypeName(p.typeId);
        p.uniqueId = formatGuid(e + 16);
        uint64_t f = le64(e + 32), last = le64(e + 40);
        p.firstLba = f;
        p.sectorCount = last >= f ? last - f + 1 : 0;
        p.name = utf16Name(e + 56, 36);
        l.partitions.push_back(std::move(p));
    }
    validatePartitions(l, firstUsable, lastUsable);
    return true;
}

void parseMbr(const ByteSource& src, DiskLayout& l, const uint8_t* mbr) {
    l.scheme = Scheme::MBR;
    char id[16];
    std::snprintf(id, sizeof id, "%08X", le32(mbr + 440));
    l.diskId = id;
    const uint32_t ss = l.sectorSize;
    int idx = 0;
    uint64_t extBase = 0;
    for (int i = 0; i < 4; ++i) {
        const uint8_t* e = mbr + 446 + i * 16;
        uint8_t type = e[4];
        if (type == 0) continue;
        if (e[0] != 0x00 && e[0] != 0x80) diag(l, Severity::Warning, "MBR entry " + std::to_string(i + 1) + " has invalid boot flag");
        Partition p;
        p.index = ++idx;
        char t[8]; std::snprintf(t, sizeof t, "0x%02X", type);
        p.typeId = t; p.typeName = mbrTypeName(type);
        p.firstLba = le32(e + 8); p.sectorCount = le32(e + 12);
        p.bootable = e[0] == 0x80;
        if (isExtended(type)) { if (!extBase) extBase = p.firstLba; }
        l.partitions.push_back(std::move(p));
    }
    uint64_t usableLast = l.totalSectors ? l.totalSectors - 1 : UINT64_MAX;
    // Walk the EBR chain with a hard limit to survive loops in corrupted tables.
    if (extBase) {
        uint64_t ebr = extBase;
        for (int guard = 0; guard < 128; ++guard) {
            std::vector<uint8_t> b(512);
            if (!src.read(ebr * ss, b.data(), 512) || b[510] != 0x55 || b[511] != 0xAA) {
                diag(l, Severity::Error, "Unreadable or invalid EBR at LBA " + std::to_string(ebr)); break;
            }
            const uint8_t* e = b.data() + 446;
            if (e[4] != 0) {
                Partition p;
                p.index = ++idx; p.logical = true;
                char t[8]; std::snprintf(t, sizeof t, "0x%02X", e[4]);
                p.typeId = t; p.typeName = mbrTypeName(e[4]);
                p.firstLba = ebr + le32(e + 8); p.sectorCount = le32(e + 12);
                l.partitions.push_back(std::move(p));
            }
            const uint8_t* n = b.data() + 446 + 16;
            if (!isExtended(n[4]) || le32(n + 8) == 0) break;
            uint64_t next = extBase + le32(n + 8);
            if (next <= ebr) { diag(l, Severity::Error, "EBR chain loops or goes backwards"); break; }
            ebr = next;
            if (guard == 127) diag(l, Severity::Error, "EBR chain too long, truncated");
        }
    }
    // Overlap check excludes the extended container, which legitimately holds logicals.
    DiskLayout tmp;
    for (auto& p : l.partitions) if (!isExtended(uint8_t(std::stoi(p.typeId, nullptr, 16)))) tmp.partitions.push_back(p);
    tmp.diagnostics.clear();
    validatePartitions(tmp, 1, usableLast);
    for (auto& d : tmp.diagnostics) l.diagnostics.push_back(d);
    for (auto& p : l.partitions)
        if (isExtended(uint8_t(std::stoi(p.typeId, nullptr, 16))) && p.firstLba + p.sectorCount - 1 > usableLast)
            diag(l, Severity::Error, "Extended partition lies outside the disk");
}

DiskLayout analyzeWith(const ByteSource& src, uint32_t ss) {
    DiskLayout l;
    l.sectorSize = ss;
    l.totalSectors = src.size() / ss;
    std::vector<uint8_t> s0(ss);
    if (src.size() < 512 || !src.read(0, s0.data(), ss)) {
        diag(l, Severity::Error, "Cannot read sector 0 (I/O error or image too small)");
        return l;
    }
    bool sig = s0[510] == 0x55 && s0[511] == 0xAA;
    bool gptProtective = sig && s0[446 + 4] == 0xEE;
    if (parseGpt(src, l)) {
        if (!gptProtective) diag(l, Severity::Warning, "GPT without protective MBR");
    } else if (gptProtective) {
        l.scheme = Scheme::GPT;
        diag(l, Severity::Error, "Protective MBR present but GPT header not found");
    } else if (sig) {
        parseMbr(src, l, s0.data());
    } else {
        l.scheme = Scheme::Raw;
        diag(l, Severity::Info, "No MBR/GPT signature: unpartitioned or filesystem directly on the device");
    }
    for (auto& p : l.partitions) p.filesystem = detectFilesystem(src, ss, p);
    return l;
}

}  // namespace

bool DiskLayout::hasErrors() const {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](auto& d) { return d.severity == Severity::Error; });
}

bool MemorySource::read(uint64_t off, void* buf, size_t len) const {
    if (off > data_.size() || len > data_.size() - off) return false;
    std::memcpy(buf, data_.data() + off, len);
    return true;
}

FileSource::FileSource(const std::string& path) {
    FILE* f = nullptr;
#ifdef _WIN32
    fopen_s(&f, path.c_str(), "rb");  // read-only by construction
#else
    f = std::fopen(path.c_str(), "rb");
#endif
    if (!f) return;
#ifdef _WIN32
    _fseeki64(f, 0, SEEK_END); size_ = uint64_t(_ftelli64(f));
#else
    fseeko(f, 0, SEEK_END); size_ = uint64_t(ftello(f));
#endif
    f_ = f;
}
FileSource::~FileSource() { if (f_) std::fclose(static_cast<FILE*>(f_)); }
bool FileSource::read(uint64_t off, void* buf, size_t len) const {
    if (!f_ || off > size_ || len > size_ - off) return false;
    FILE* f = static_cast<FILE*>(f_);
#ifdef _WIN32
    if (_fseeki64(f, int64_t(off), SEEK_SET) != 0) return false;
#else
    if (fseeko(f, off_t(off), SEEK_SET) != 0) return false;
#endif
    return std::fread(buf, 1, len, f) == len;
}

namespace {
// Presents the first `limit` bytes of another source (used to hide a fixed-VHD footer).
class LimitedSource final : public ByteSource {
public:
    LimitedSource(const ByteSource& s, uint64_t limit) : s_(s), limit_(limit) {}
    uint64_t size() const override { return limit_; }
    bool read(uint64_t off, void* buf, size_t len) const override {
        if (off > limit_ || len > limit_ - off) return false;
        return s_.read(off, buf, len);
    }
private:
    const ByteSource& s_;
    uint64_t limit_;
};
}  // namespace

DiskLayout analyze(const ByteSource& full, uint32_t sectorSize) {
    // A fixed VHD ends with a 512-byte "conectix" footer that is not part of the disk.
    uint8_t cookie[8];
    bool vhd = full.size() > 512 && full.read(full.size() - 512, cookie, 8) && !std::memcmp(cookie, "conectix", 8);
    LimitedSource limited(full, vhd ? full.size() - 512 : full.size());
    const ByteSource& src = vhd ? static_cast<const ByteSource&>(limited) : full;
    if (sectorSize) return analyzeWith(src, sectorSize);
    // Auto-detect: prefer the sector size at which a GPT header is found.
    for (uint32_t ss : {512u, 4096u}) {
        uint8_t sig[8];
        if (src.size() >= uint64_t(ss) * 2 && src.read(ss, sig, 8) && !std::memcmp(sig, "EFI PART", 8))
            return analyzeWith(src, ss);
    }
    return analyzeWith(src, 512);
}

const char* schemeName(Scheme s) {
    switch (s) { case Scheme::MBR: return "MBR"; case Scheme::GPT: return "GPT"; case Scheme::Raw: return "RAW"; default: return "Unknown"; }
}

uint32_t crc32(const void* data, size_t len) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) { uint32_t c = i; for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; t[i] = c; }
        return t;
    }();
    uint32_t c = 0xFFFFFFFFu;
    auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

std::string formatGuid(const uint8_t* g) {
    char b[40];
    std::snprintf(b, sizeof b, "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", le32(g), le16(g + 4), le16(g + 6),
                  g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
    return b;
}

std::string humanSize(uint64_t bytes) {
    static const char* u[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    double v = double(bytes); int i = 0;
    while (v >= 1024 && i < 5) { v /= 1024; ++i; }
    char b[32];
    std::snprintf(b, sizeof b, i ? "%.2f %s" : "%.0f %s", v, u[i]);
    return b;
}

}  // namespace diskpro
