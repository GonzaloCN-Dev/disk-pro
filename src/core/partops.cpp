// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
#include "partops.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <random>

namespace diskpro {

namespace {

constexpr uint32_t kEntries = 128, kEntrySize = 128;

void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i)); }
void put64(uint8_t* p, uint64_t v) { for (int i = 0; i < 8; ++i) p[i] = uint8_t(v >> (8 * i)); }
uint32_t get32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint64_t get64(const uint8_t* p) { return uint64_t(get32(p)) | uint64_t(get32(p + 4)) << 32; }

void randomGuid(uint8_t g[16]) {
    std::random_device rd;
    for (int i = 0; i < 16; i += 4) put32(g + i, rd());
    g[7] = uint8_t((g[7] & 0x0F) | 0x40);   // version 4 (in mixed-endian layout byte 7 holds the version nibble)
    g[8] = uint8_t((g[8] & 0x3F) | 0x80);
}

int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

const char* kGptBasic = "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7";
const char* kGptEfi = "C12A7328-F81F-11D2-BA4B-00A0C93EC93B";
const char* kGptLinux = "0FC63DAF-8483-4772-8E79-3D69D8477DE4";
const char* kGptSwap = "0657FD6D-A4AB-43C4-84E5-0933C84B4F4F";

std::string lower(std::string s) { for (auto& c : s) c = char(std::tolower((unsigned char)c)); return s; }

bool resolveMbrType(const std::string& in, uint8_t& out) {
    std::string s = lower(in);
    if (s == "ntfs" || s == "exfat" || s == "basic") { out = 0x07; return true; }
    if (s == "fat32") { out = 0x0C; return true; }
    if (s == "fat16") { out = 0x0E; return true; }
    if (s == "efi") { out = 0xEF; return true; }
    if (s == "linux") { out = 0x83; return true; }
    if (s == "swap") { out = 0x82; return true; }
    if (s.size() >= 3 && s[0] == '0' && s[1] == 'x') {
        unsigned v = 0; size_t n = 0;
        for (size_t i = 2; i < s.size(); ++i) { int h = hexv(s[i]); if (h < 0) return false; v = v * 16 + unsigned(h); ++n; }
        if (n == 0 || n > 2 || v == 0 || v == 0x05 || v == 0x0F || v == 0x85 || v == 0xEE) return false;   // no extended / protective
        out = uint8_t(v); return true;
    }
    return false;
}

bool resolveGptType(const std::string& in, uint8_t out[16]) {
    std::string s = lower(in);
    const char* g = nullptr;
    if (s == "ntfs" || s == "exfat" || s == "fat32" || s == "fat16" || s == "basic") g = kGptBasic;
    else if (s == "efi") g = kGptEfi;
    else if (s == "linux") g = kGptLinux;
    else if (s == "swap") g = kGptSwap;
    if (g) return parseGuid(g, out);
    uint8_t z[16] = {};
    if (!parseGuid(in, out)) return false;
    return std::memcmp(out, z, 16) != 0;   // the all-zero GUID means "unused"
}

void utf16Put(uint8_t* dst, const std::string& name) {   // UTF-8 BMP -> UTF-16LE, up to 36 units
    size_t n = 0;
    for (size_t i = 0; i < name.size() && n < 36;) {
        uint32_t c = uint8_t(name[i]);
        size_t extra = 0;
        if (c >= 0xF0) { c = '?'; extra = 3; }
        else if (c >= 0xE0) { c &= 0x0F; extra = 2; }
        else if (c >= 0xC0) { c &= 0x1F; extra = 1; }
        else if (c >= 0x80) { c = '?'; }
        for (size_t k = 0; k < extra && i + 1 + k < name.size(); ++k) c = (c << 6) | (uint8_t(name[i + 1 + k]) & 0x3F);
        i += 1 + extra;
        if (c >= 0x10000) c = '?';
        dst[n * 2] = uint8_t(c); dst[n * 2 + 1] = uint8_t(c >> 8); ++n;
    }
}

struct Model {
    bool gpt = false;
    uint32_t ss = 512;
    uint64_t total = 0;
    // MBR
    std::vector<uint8_t> sector0;
    // GPT
    std::vector<uint8_t> header;    // ss bytes (primary)
    std::vector<uint8_t> entries;   // kEntries * kEntrySize
    uint64_t entrySectors() const { return (uint64_t(kEntries) * kEntrySize + ss - 1) / ss; }
    uint64_t firstUsable() const { return gpt ? 2 + entrySectors() : 1; }
    uint64_t lastUsable() const { return gpt ? total - 2 - entrySectors() : total - 1; }
};

struct Slot { bool used; uint64_t first, count; };

// Slots in the same order analyze() numbers them (non-empty entries, in table order).
std::vector<int> usedSlots(const Model& m) {
    std::vector<int> v;
    static const uint8_t zero[16] = {};
    if (m.gpt) { for (uint32_t i = 0; i < kEntries; ++i) if (std::memcmp(&m.entries[size_t(i) * kEntrySize], zero, 16)) v.push_back(int(i)); }
    else { for (int i = 0; i < 4; ++i) if (m.sector0[446 + i * 16 + 4] != 0) v.push_back(i); }
    return v;
}

Slot slotInfo(const Model& m, int slot) {
    if (m.gpt) {
        const uint8_t* e = &m.entries[size_t(slot) * kEntrySize];
        uint64_t f = get64(e + 32), l = get64(e + 40);
        return {true, f, l >= f ? l - f + 1 : 0};
    }
    const uint8_t* e = &m.sector0[446 + slot * 16];
    return {e[4] != 0, get32(e + 8), get32(e + 12)};
}

void setSlot(Model& m, int slot, uint64_t first, uint64_t count) {
    if (m.gpt) {
        uint8_t* e = &m.entries[size_t(slot) * kEntrySize];
        put64(e + 32, first); put64(e + 40, first + count - 1);
    } else {
        uint8_t* e = &m.sector0[446 + slot * 16];
        e[1] = 0xFE; e[2] = 0xFF; e[3] = 0xFF; e[5] = 0xFE; e[6] = 0xFF; e[7] = 0xFF;   // CHS unused (LBA addressing)
        put32(e + 8, uint32_t(first)); put32(e + 12, uint32_t(count));
    }
}

void clearSlot(Model& m, int slot) {
    if (m.gpt) std::memset(&m.entries[size_t(slot) * kEntrySize], 0, kEntrySize);
    else std::memset(&m.sector0[446 + slot * 16], 0, 16);
}

bool overlapsOthers(const Model& m, uint64_t first, uint64_t count, int ignoreSlot) {
    for (int s : usedSlots(m)) {
        if (s == ignoreSlot) continue;
        Slot o = slotInfo(m, s);
        if (first < o.first + o.count && o.first < first + count) return true;
    }
    return false;
}

void buildGptHeaders(const Model& m, std::vector<uint8_t>& primary, std::vector<uint8_t>& backup) {
    primary = m.header;
    const uint64_t es = m.entrySectors();
    put64(&primary[24], 1);
    put64(&primary[32], m.total - 1);
    put64(&primary[72], 2);
    put32(&primary[80], kEntries); put32(&primary[84], kEntrySize);
    put32(&primary[88], crc32(m.entries.data(), m.entries.size()));
    put32(&primary[16], 0);
    put32(&primary[16], crc32(primary.data(), get32(&primary[12])));
    backup = primary;
    put64(&backup[24], m.total - 1);
    put64(&backup[32], 1);
    put64(&backup[72], m.total - 1 - es);
    put32(&backup[16], 0);
    put32(&backup[16], crc32(backup.data(), get32(&backup[12])));
}

std::vector<TableWrite> serialize(const Model& m, bool includeSector0) {
    std::vector<TableWrite> w;
    if (!m.gpt) { w.push_back({0, m.sector0}); return w; }
    std::vector<uint8_t> p, b;
    buildGptHeaders(m, p, b);
    if (includeSector0) w.push_back({0, m.sector0});
    w.push_back({uint64_t(m.ss), p});
    std::vector<uint8_t> ent(m.entrySectors() * m.ss, 0);
    std::memcpy(ent.data(), m.entries.data(), m.entries.size());
    w.push_back({2ull * m.ss, ent});
    w.push_back({(m.total - 1 - m.entrySectors()) * m.ss, ent});
    w.push_back({(m.total - 1) * m.ss, b});
    return w;
}

bool loadModel(const ByteSource& disk, uint32_t ss, Model& m, std::string& err) {
    m.ss = ss; m.total = disk.size() / ss;
    m.sector0.assign(ss, 0);
    if (!disk.read(0, m.sector0.data(), ss)) { err = "cannot read sector 0"; return false; }
    DiskLayout l = analyze(disk, ss);
    if (l.scheme != Scheme::MBR && l.scheme != Scheme::GPT) { err = "the disk has no MBR/GPT table; initialize it first"; return false; }
    if (l.hasErrors()) { err = "the current partition table has errors; refusing to edit it"; return false; }
    if (l.scheme == Scheme::MBR) {
        for (auto& p : l.partitions) if (p.logical || p.typeName == "Extended") { err = "extended/logical MBR partitions are not supported by this version"; return false; }
        if (m.sector0[510] != 0x55 || m.sector0[511] != 0xAA) { err = "invalid MBR signature"; return false; }
        m.gpt = false;
        return true;
    }
    m.gpt = true;
    m.header.assign(ss, 0);
    if (!disk.read(ss, m.header.data(), ss)) { err = "cannot read the GPT header"; return false; }
    uint32_t hsz = get32(&m.header[12]);
    if (get32(&m.header[80]) != kEntries || get32(&m.header[84]) != kEntrySize || get64(&m.header[72]) != 2 || hsz < 92 || hsz > ss) {
        err = "non-standard GPT geometry (only 128 entries of 128 bytes at LBA 2 are supported)"; return false;
    }
    if (get64(&m.header[32]) != m.total - 1) { err = "GPT backup header is not at the last sector"; return false; }
    m.entries.assign(size_t(kEntries) * kEntrySize, 0);
    if (!disk.read(2ull * ss, m.entries.data(), m.entries.size())) { err = "cannot read the GPT entry array"; return false; }
    return true;
}

void fillInit(Model& m, bool gpt, const PartOp& op) {
    m.gpt = gpt;
    m.sector0.assign(m.ss, 0);
    if (!gpt) {
        uint32_t sig = op.mbrSignature;
        if (!sig) { std::random_device rd; sig = rd(); }
        put32(&m.sector0[440], sig);
    } else {
        uint8_t* e = &m.sector0[446];
        e[2] = 0x02; e[4] = 0xEE; e[5] = 0xFF; e[6] = 0xFF; e[7] = 0xFF;
        put32(e + 8, 1);
        put32(e + 12, uint32_t(std::min<uint64_t>(m.total - 1, 0xFFFFFFFFull)));
        m.header.assign(m.ss, 0);
        std::memcpy(&m.header[0], "EFI PART", 8);
        m.header[10] = 0x01;                 // revision 1.0
        put32(&m.header[12], 92);
        put64(&m.header[40], m.firstUsable());
        put64(&m.header[48], m.lastUsable());
        uint8_t g[16];
        if (op.uniqueId.empty() || !parseGuid(op.uniqueId, g)) randomGuid(g);
        std::memcpy(&m.header[56], g, 16);
        m.entries.assign(size_t(kEntries) * kEntrySize, 0);
    }
    m.sector0[510] = 0x55; m.sector0[511] = 0xAA;
}

}  // namespace

bool parseGuid(const std::string& s, uint8_t out[16]) {
    // text 8-4-4-4-12 -> mixed endian (first three groups little-endian)
    if (s.size() != 36 || s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-') return false;
    uint8_t b[16]; int n = 0;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '-') { ++i; continue; }
        int h = hexv(s[i]), l = i + 1 < s.size() ? hexv(s[i + 1]) : -1;
        if (h < 0 || l < 0 || n >= 16) return false;
        b[n++] = uint8_t(h * 16 + l); i += 2;
    }
    if (n != 16) return false;
    out[0] = b[3]; out[1] = b[2]; out[2] = b[1]; out[3] = b[0];
    out[4] = b[5]; out[5] = b[4]; out[6] = b[7]; out[7] = b[6];
    std::memcpy(out + 8, b + 8, 8);
    return true;
}

uint64_t alignSectors(uint32_t ss) { return ss ? (1ull << 20) / ss : 2048; }

PartOpPlan planPartOp(const ByteSource& disk, const TargetFacts& t, const PartOp& op) {
    PartOpPlan p;
    p.token = "EDIT DISK " + std::to_string(t.number);
    auto block = [&](const std::string& s) { p.blockers.push_back(s); };
    if (t.isSystemDisk) block("Target is the SYSTEM disk: writing is forbidden.");
    if (!t.isVirtual) block("Target is not a virtual disk: this version only writes to virtual disks (VHD/VHDX).");
    if (t.size > kMaxWritableDisk) block("Target is larger than 4 GiB: this version refuses to write to it.");
    if (t.size == 0) block("Target size is unknown.");
    if (t.sectorSize != 512 && t.sectorSize != 4096) block("Unsupported sector size.");
    if (disk.size() != t.size) block("The readable size does not match the target size.");
    if (!p.blockers.empty()) return p;

    const uint32_t ss = t.sectorSize;
    Model m; m.ss = ss; m.total = disk.size() / ss;
    const uint64_t A = alignSectors(ss);
    using K = PartOp::Kind;

    if (op.kind == K::InitMbr || op.kind == K::InitGpt) {
        if (m.total < 2048) { block("The disk is too small to hold a partition table."); return p; }
        DiskLayout cur = analyze(disk, ss);
        if (cur.scheme == Scheme::MBR || cur.scheme == Scheme::GPT)
            p.warnings.push_back(std::string("The current ") + schemeName(cur.scheme) + " table with " + std::to_string(cur.partitions.size()) +
                                 " partition(s) will be ERASED. Partition contents are not touched, but become unreachable.");
        fillInit(m, op.kind == K::InitGpt, op);
        p.writes = serialize(m, true);
        if (op.kind == K::InitMbr && cur.scheme == Scheme::GPT) {
            // a new MBR over a GPT disk must also remove the GPT headers and entry arrays, or the disk still reads as GPT
            const uint64_t es = m.entrySectors();
            p.writes.push_back({uint64_t(ss), std::vector<uint8_t>((1 + es) * ss, 0)});
            p.writes.push_back({(m.total - 1 - es) * ss, std::vector<uint8_t>((es + 1) * ss, 0)});
        }
        p.summary = op.kind == K::InitGpt ? "Create a new empty GPT table" : "Create a new empty MBR table";
        p.allowed = true;
        return p;
    }

    std::string err;
    if (!loadModel(disk, ss, m, err)) { block(err); return p; }
    auto slots = usedSlots(m);
    auto slotOf = [&](int idx, int& slot) -> bool {
        if (idx < 1 || idx > int(slots.size())) { block("Partition " + std::to_string(idx) + " does not exist."); return false; }
        slot = slots[size_t(idx - 1)]; return true;
    };
    auto checkRange = [&](uint64_t first, uint64_t count, int ignoreSlot) {
        if (count == 0) { block("The size is zero."); return; }
        if (first % A) block("The start must be aligned to 1 MiB.");
        if (first < m.firstUsable() || count > m.lastUsable() + 1 || first > m.lastUsable() + 1 - count) block("The range lies outside the usable area of the disk.");
        else if (overlapsOthers(m, first, count, ignoreSlot)) block("The range overlaps another partition.");
        if (!m.gpt && (first > 0xFFFFFFFFull || count > 0xFFFFFFFFull)) block("MBR cannot address that range.");
        if (count % A && first + count != m.lastUsable() + 1) p.warnings.push_back("The size is not a multiple of 1 MiB.");
    };

    switch (op.kind) {
        case K::Create: {
            int free = -1;
            for (int i = 0; i < (m.gpt ? int(kEntries) : 4); ++i) if (std::find(slots.begin(), slots.end(), i) == slots.end()) { free = i; break; }
            if (free < 0) { block("There is no free entry in the partition table."); break; }
            checkRange(op.firstLba, op.sectorCount, -1);
            uint8_t mt = 0; uint8_t gt[16] = {};
            if (m.gpt ? !resolveGptType(op.type.empty() ? "basic" : op.type, gt) : !resolveMbrType(op.type.empty() ? "ntfs" : op.type, mt)) block("Unknown or forbidden partition type.");
            if (!p.blockers.empty()) break;
            setSlot(m, free, op.firstLba, op.sectorCount);
            if (m.gpt) {
                uint8_t* e = &m.entries[size_t(free) * kEntrySize];
                std::memcpy(e, gt, 16);
                uint8_t g[16];
                if (op.uniqueId.empty() || !parseGuid(op.uniqueId, g)) randomGuid(g);
                std::memcpy(e + 16, g, 16);
                utf16Put(e + 56, op.name);
            } else m.sector0[446 + free * 16 + 4] = mt;
            p.warnings.push_back("The new partition is empty (no file system): format it before use.");
            p.summary = "Create a partition at LBA " + std::to_string(op.firstLba) + ", " + humanSize(op.sectorCount * ss);
            break;
        }
        case K::Delete: {
            int s; if (!slotOf(op.index, s)) break;
            clearSlot(m, s);
            p.warnings.push_back("The partition entry is removed. Its data stays on disk but becomes unreachable until recreated identically.");
            p.summary = "Delete partition " + std::to_string(op.index);
            break;
        }
        case K::SetType: {
            int s; if (!slotOf(op.index, s)) break;
            uint8_t mt = 0; uint8_t gt[16] = {};
            if (m.gpt ? !resolveGptType(op.type, gt) : !resolveMbrType(op.type, mt)) { block("Unknown or forbidden partition type."); break; }
            if (m.gpt) std::memcpy(&m.entries[size_t(s) * kEntrySize], gt, 16); else m.sector0[446 + s * 16 + 4] = mt;
            p.warnings.push_back("Only the type label changes; the file system is not converted.");
            p.summary = "Change the type of partition " + std::to_string(op.index);
            break;
        }
        case K::SetActive: {
            if (m.gpt) { block("The active flag exists only in MBR tables."); break; }
            int s; if (!slotOf(op.index, s)) break;
            for (int i = 0; i < 4; ++i) m.sector0[446 + i * 16] = uint8_t(i == s ? 0x80 : 0x00);
            p.summary = "Mark partition " + std::to_string(op.index) + " as active";
            break;
        }
        case K::Resize: {
            int s; if (!slotOf(op.index, s)) break;
            checkRange(op.firstLba, op.sectorCount, s);
            if (!p.blockers.empty()) break;
            Slot old = slotInfo(m, s);
            setSlot(m, s, op.firstLba, op.sectorCount);
            p.warnings.push_back("Only the table entry changes. The file system is NOT resized or moved: shrinking can cut data off, moving makes the data unreachable.");
            if (op.sectorCount < old.count) p.warnings.push_back("The partition is being made smaller: data beyond the new end becomes inaccessible.");
            p.summary = "Resize/move partition " + std::to_string(op.index);
            break;
        }
        case K::Merge: {
            int s1, s2;
            if (!slotOf(op.index, s1) || !slotOf(op.index2, s2)) break;
            if (s1 == s2) { block("Choose two different partitions."); break; }
            Slot a = slotInfo(m, s1), b = slotInfo(m, s2);
            if (a.first > b.first) { std::swap(a, b); std::swap(s1, s2); }
            if (a.first + a.count != b.first) { block("The partitions are not contiguous."); break; }
            setSlot(m, s1, a.first, a.count + b.count);
            clearSlot(m, s2);
            p.warnings.push_back("The second partition is removed and the first one covers both. The first file system is NOT extended and the second one's data becomes unreachable.");
            p.summary = "Merge two contiguous partitions";
            break;
        }
        default: block("Unsupported operation."); break;
    }
    if (!p.blockers.empty()) return p;
    p.writes = serialize(m, false);
    p.allowed = !p.writes.empty();
    return p;
}

PartOpResult applyPartOp(const PartOpPlan& plan, const std::string& typed, ByteSink& sink, const ByteSource& readback, uint32_t ss) {
    PartOpResult r;
    if (!plan.allowed || plan.writes.empty() || !plan.blockers.empty()) { r.error = "operation not allowed by the plan"; return r; }
    if (typed != plan.token) { r.error = "confirmation text does not match"; return r; }
    const uint64_t size = readback.size();
    if (size > kMaxWritableDisk || (ss != 512 && ss != 4096)) { r.error = "disk not allowed"; return r; }
    // Every write must stay inside the table areas: the first 34 sectors or the last 33.
    const uint64_t es = (uint64_t(kEntries) * kEntrySize + ss - 1) / ss;
    const uint64_t head = (2 + es) * ss, tailStart = size - (1 + es) * ss;
    for (auto& w : plan.writes) {
        if (w.data.empty() || w.offset % ss || w.data.size() % ss || w.offset > size || w.data.size() > size - w.offset) { r.error = "invalid write range"; return r; }
        const bool inHead = w.offset + w.data.size() <= head, inTail = w.offset >= tailStart;
        if (!inHead && !inTail) { r.error = "write outside the table areas"; return r; }
    }
    for (auto& w : plan.writes) {
        r.written = true;
        if (!sink.write(w.offset, w.data.data(), w.data.size())) { r.error = "write failed"; return r; }
    }
    if (!sink.flush()) { r.error = "flush failed"; return r; }
    for (auto& w : plan.writes) {
        std::vector<uint8_t> back(w.data.size());
        if (!readback.read(w.offset, back.data(), back.size()) || std::memcmp(back.data(), w.data.data(), back.size()) != 0) {
            r.error = "verification failed: data read back differs"; return r;
        }
    }
    DiskLayout l = analyze(readback, ss);
    if (l.hasErrors()) { r.error = "verification failed: the resulting table has errors"; return r; }
    r.ok = true;
    return r;
}

}  // namespace diskpro
