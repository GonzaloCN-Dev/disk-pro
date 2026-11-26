// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
#include "format.h"
#include <algorithm>
#include <cstring>
#include <random>

namespace diskpro {

namespace {
void p16(uint8_t* p, uint32_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
void p32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i)); }
void p64(uint8_t* p, uint64_t v) { for (int i = 0; i < 8; ++i) p[i] = uint8_t(v >> (8 * i)); }
uint32_t ck32(uint32_t c, uint8_t b) { return ((c << 31) | (c >> 1)) + b; }
uint32_t log2u(uint32_t v) { uint32_t n = 0; while (v > 1) { v >>= 1; ++n; } return n; }
}  // namespace

FormatPlan planFormat(const ByteSource& disk, const TargetFacts& t, const FormatOp& op) {
    FormatPlan p;
    p.token = "FORMAT DISK " + std::to_string(t.number) + " PARTITION " + std::to_string(op.index);
    auto block = [&](const std::string& s) { p.blockers.push_back(s); };
    if (t.isSystemDisk) block("Target is the SYSTEM disk: writing is forbidden.");
    if (!t.isVirtual) block("Target is not a virtual disk: this version only writes to virtual disks (VHD/VHDX).");
    if (t.size > kMaxWritableDisk) block("Target is larger than 4 GiB: this version refuses to write to it.");
    if (t.size == 0 || disk.size() != t.size) block("Target size is unknown or inconsistent.");
    if (t.sectorSize != 512 && t.sectorSize != 4096) block("Unsupported sector size.");
    std::string fs = op.fs; for (auto& c : fs) c = char(std::tolower((unsigned char)c));
    if (fs != "fat32" && fs != "exfat") block("Only FAT32 and exFAT can be formatted by this version (NTFS is not implemented).");
    std::string label = op.label;
    if (label.size() > 11) block("The label is longer than 11 characters.");
    for (auto& c : label) {
        c = char(std::toupper((unsigned char)c));
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '-')) { block("The label may only contain A-Z, 0-9, space, _ and -."); break; }
    }
    if (!p.blockers.empty()) return p;

    const uint32_t ss = t.sectorSize;
    DiskLayout l = analyze(disk, ss);
    if ((l.scheme != Scheme::MBR && l.scheme != Scheme::GPT) || l.hasErrors()) { block("The disk needs a valid MBR/GPT table without errors."); return p; }
    if (op.index < 1 || op.index > int(l.partitions.size())) { block("Partition " + std::to_string(op.index) + " does not exist."); return p; }
    const Partition& part = l.partitions[size_t(op.index - 1)];
    if (part.logical || part.typeName == "Extended") { block("Extended/logical partitions are not supported."); return p; }
    p.partOffset = part.firstLba * ss; p.partLength = part.sectorCount * ss;
    const uint64_t T = part.sectorCount;
    if (p.partOffset + p.partLength > disk.size()) { block("The partition lies outside the disk."); return p; }
    if (T > 0xFFFFFFFFull) { block("The partition is too large for this formatter."); return p; }

    if (fs == "exfat") {
        if (T * ss < (8ull << 20)) { block("The partition is too small for exFAT (minimum 8 MiB)."); return p; }
        const uint32_t spc = 4096 / ss, cb = 4096, fatOff = 24;
        uint32_t fatLen = 1, heapOff = 0, clusters = 0;
        for (int i = 0; i < 64; ++i) {
            heapOff = fatOff + fatLen;
            if (T <= heapOff + 8) { block("The partition is too small for exFAT."); return p; }
            clusters = uint32_t((T - heapOff) / spc);
            uint32_t need = uint32_t((uint64_t(clusters + 2) * 4 + ss - 1) / ss);
            if (need == fatLen) break;
            fatLen = need;
        }
        const uint32_t bmBytes = (clusters + 7) / 8, bmClusters = (bmBytes + cb - 1) / cb;
        const uint32_t upCluster = 2 + bmClusters, rootCluster = upCluster + 1;
        if (clusters < bmClusters + 3) { block("The partition is too small for exFAT."); return p; }
        // compressed identity up-case table, upper-casing a-z only
        std::vector<uint16_t> up = {0xFFFF, 0x0061};
        for (uint16_t c = 'A'; c <= 'Z'; ++c) up.push_back(c);
        up.push_back(0xFFFF); up.push_back(uint16_t(0x10000 - 0x7B));
        std::vector<uint8_t> upb(up.size() * 2);
        for (size_t i = 0; i < up.size(); ++i) { upb[2 * i] = uint8_t(up[i]); upb[2 * i + 1] = uint8_t(up[i] >> 8); }
        uint32_t upSum = 0; for (uint8_t b : upb) upSum = ck32(upSum, b);

        uint32_t vid = op.volumeId;
        if (!vid) { std::random_device rd; vid = rd(); }
        std::vector<uint8_t> region(size_t(12) * ss, 0);
        uint8_t* b = region.data();
        b[0] = 0xEB; b[1] = 0x76; b[2] = 0x90; std::memcpy(b + 3, "EXFAT   ", 8);
        p64(b + 64, part.firstLba); p64(b + 72, T);
        p32(b + 80, fatOff); p32(b + 84, fatLen); p32(b + 88, heapOff); p32(b + 92, clusters); p32(b + 96, rootCluster); p32(b + 100, vid);
        b[104] = 0x00; b[105] = 0x01;                 // revision 1.00
        b[108] = uint8_t(log2u(ss)); b[109] = uint8_t(log2u(spc)); b[110] = 1; b[111] = 0x80; b[112] = 0xFF;
        b[510] = 0x55; b[511] = 0xAA; if (ss > 512) { b[ss - 2] = 0x55; b[ss - 1] = 0xAA; }
        for (int s = 1; s <= 8; ++s) { uint8_t* e = b + size_t(s) * ss; e[ss - 2] = 0x55; e[ss - 1] = 0xAA; }   // extended boot signature 0xAA550000
        uint32_t sum = 0;
        for (size_t i = 0; i < size_t(11) * ss; ++i) { if (i == 106 || i == 107 || i == 112) continue; sum = ck32(sum, b[i]); }
        for (size_t i = 0; i < ss; i += 4) p32(b + size_t(11) * ss + i, sum);

        std::vector<uint8_t> fat(size_t(fatLen) * ss, 0);
        p32(&fat[0], 0xFFFFFFF8); p32(&fat[4], 0xFFFFFFFF);
        auto chain = [&](uint32_t first, uint32_t count) { for (uint32_t k = 0; k < count; ++k) p32(&fat[size_t(first + k) * 4], k + 1 == count ? 0xFFFFFFFFu : first + k + 1); };
        chain(2, bmClusters); chain(upCluster, 1); chain(rootCluster, 1);

        std::vector<uint8_t> bm(size_t(bmClusters) * cb, 0);
        for (uint32_t c = 0; c < bmClusters + 2; ++c) bm[c / 8] |= uint8_t(1u << (c % 8));
        std::vector<uint8_t> upc(cb, 0); std::memcpy(upc.data(), upb.data(), upb.size());
        std::vector<uint8_t> root(cb, 0);
        root[0] = 0x83; root[1] = uint8_t(label.size());
        for (size_t i = 0; i < label.size(); ++i) { root[2 + 2 * i] = uint8_t(label[i]); }
        root[32] = 0x81; p32(&root[32 + 20], 2); p64(&root[32 + 24], bmBytes);
        root[64] = 0x82; p32(&root[64 + 4], upSum); p32(&root[64 + 20], upCluster); p64(&root[64 + 24], upb.size());

        const uint64_t base = p.partOffset;
        auto cl = [&](uint32_t c) { return base + (uint64_t(heapOff) + uint64_t(c - 2) * spc) * ss; };
        p.writes.push_back({base, region});
        p.writes.push_back({base + 12ull * ss, region});
        p.writes.push_back({base + uint64_t(fatOff) * ss, fat});
        p.writes.push_back({cl(2), bm});
        p.writes.push_back({cl(upCluster), upc});
        p.writes.push_back({cl(rootCluster), root});
        p.clusterBytes = cb; p.clusters = clusters;
        p.warnings.push_back("ALL data in the partition becomes unreachable (quick format: only file-system metadata is written).");
        if (!part.filesystem.empty()) p.warnings.push_back("The partition currently holds " + part.filesystem + ", which will be replaced.");
        p.summary = "Format partition " + std::to_string(op.index) + " as exFAT (4096-byte clusters)";
        p.allowed = true;
        return p;
    }
    // Cluster size: bigger partitions get bigger clusters, but FAT32 needs at least 65525 clusters.
    const uint32_t R = 32, eps = ss / 4;
    uint32_t spc = ss == 512 ? (T * ss <= (260ull << 20) ? 1 : 8) : 1;
    uint32_t fatSz = 0, clusters = 0;
    auto layoutFor = [&](uint32_t s) {
        fatSz = 1;
        for (int i = 0; i < 64; ++i) {
            if (T <= R + 2ull * fatSz) { clusters = 0; return; }
            clusters = uint32_t((T - R - 2ull * fatSz) / s);
            uint32_t need = (clusters + 2 + eps - 1) / eps;
            if (need == fatSz) return;
            fatSz = need;
        }
    };
    layoutFor(spc);
    while (clusters < 65525 && spc > 1) { spc /= 2; layoutFor(spc); }
    if (clusters < 65525) { block("The partition is too small for FAT32 (it needs at least about 33 MiB with 512-byte sectors)."); return p; }
    if (clusters > 0x0FFFFFF0) { block("Too many clusters."); return p; }
    p.clusterBytes = spc * ss; p.clusters = clusters;

    uint32_t vid = op.volumeId;
    if (!vid) { std::random_device rd; vid = rd(); }
    std::vector<uint8_t> boot(ss, 0);
    boot[0] = 0xEB; boot[1] = 0x58; boot[2] = 0x90;
    std::memcpy(&boot[3], "DISKPRO ", 8);
    p16(&boot[11], ss); boot[13] = uint8_t(spc); p16(&boot[14], R); boot[16] = 2;
    p16(&boot[17], 0); p16(&boot[19], 0); boot[21] = 0xF8; p16(&boot[22], 0);
    p16(&boot[24], 63); p16(&boot[26], 255);
    p32(&boot[28], uint32_t(part.firstLba)); p32(&boot[32], uint32_t(T));
    p32(&boot[36], fatSz); p16(&boot[40], 0); p16(&boot[42], 0); p32(&boot[44], 2);
    p16(&boot[48], 1); p16(&boot[50], 6);
    boot[64] = 0x80; boot[66] = 0x29; p32(&boot[67], vid);
    std::memset(&boot[71], ' ', 11);
    std::memcpy(&boot[71], label.data(), label.size());
    std::memcpy(&boot[82], "FAT32   ", 8);
    boot[510] = 0x55; boot[511] = 0xAA;
    if (ss > 512) { boot[ss - 2] = 0x55; boot[ss - 1] = 0xAA; }

    std::vector<uint8_t> info(ss, 0);
    p32(&info[0], 0x41615252); p32(&info[484], 0x61417272);
    p32(&info[488], clusters - 1); p32(&info[492], 3); p32(&info[508], 0xAA550000);
    if (ss > 512) { info[ss - 2] = 0x55; info[ss - 1] = 0xAA; }

    std::vector<uint8_t> fat(size_t(fatSz) * ss, 0);
    p32(&fat[0], 0x0FFFFFF8); p32(&fat[4], 0x0FFFFFFF); p32(&fat[8], 0x0FFFFFFF);

    const uint64_t base = p.partOffset;
    p.writes.push_back({base + 0, boot});
    p.writes.push_back({base + 1ull * ss, info});
    p.writes.push_back({base + 6ull * ss, boot});
    p.writes.push_back({base + 7ull * ss, info});
    p.writes.push_back({base + 2ull * ss, std::vector<uint8_t>(ss * 4ull, 0)});   // sectors 2-5: boot-code extension area
    p.writes.push_back({base + uint64_t(R) * ss, fat});
    p.writes.push_back({base + (uint64_t(R) + fatSz) * ss, fat});
    std::vector<uint8_t> root(size_t(spc) * ss, 0);   // root directory cluster; the label lives here as a volume-label entry
    if (!label.empty()) {
        std::memset(root.data(), ' ', 11);
        std::memcpy(root.data(), label.data(), label.size());
        root[11] = 0x08;
    }
    p.writes.push_back({base + (uint64_t(R) + 2ull * fatSz) * ss, root});
    p.warnings.push_back("ALL data in the partition becomes unreachable (quick format: only file-system metadata is written).");
    if (!part.filesystem.empty()) p.warnings.push_back("The partition currently holds " + part.filesystem + ", which will be replaced.");
    p.summary = "Format partition " + std::to_string(op.index) + " as FAT32 (" + std::to_string(spc * ss) + "-byte clusters)";
    p.allowed = true;
    return p;
}

PartOpResult applyFormat(const FormatPlan& plan, const std::string& typed, ByteSink& sink, const ByteSource& readback) {
    PartOpResult r;
    if (!plan.allowed || plan.writes.empty() || !plan.blockers.empty()) { r.error = "operation not allowed by the plan"; return r; }
    if (typed != plan.token) { r.error = "confirmation text does not match"; return r; }
    const uint64_t size = readback.size();
    if (size > kMaxWritableDisk) { r.error = "disk not allowed"; return r; }
    for (auto& w : plan.writes) {
        if (w.data.empty() || w.offset < plan.partOffset || w.offset > plan.partOffset + plan.partLength ||
            w.data.size() > plan.partOffset + plan.partLength - w.offset || w.offset + w.data.size() > size) {
            r.error = "write outside the partition"; return r;
        }
    }
    if (!sink.prepare(plan.partOffset, plan.partLength)) { r.error = "the volume in this partition is in use and could not be locked (close any program using it)"; return r; }
    for (auto& w : plan.writes) {
        r.written = true;
        for (size_t o = 0; o < w.data.size(); o += (1u << 20)) {
            size_t n = std::min<size_t>(1u << 20, w.data.size() - o);
            if (!sink.write(w.offset + o, w.data.data() + o, n)) { r.error = "write failed"; return r; }
        }
    }
    if (!sink.flush()) { r.error = "flush failed"; return r; }
    for (auto& w : plan.writes) {
        std::vector<uint8_t> back(w.data.size());
        if (!readback.read(w.offset, back.data(), back.size()) || std::memcmp(back.data(), w.data.data(), back.size()) != 0) {
            r.error = "verification failed: data read back differs"; return r;
        }
    }
    r.ok = true;
    return r;
}

}  // namespace diskpro