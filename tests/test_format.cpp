// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
#include <cstdio>
#include <cstring>
#include "format.h"

using namespace diskpro;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

class MemSink final : public ByteSink {
public:
    explicit MemSink(MemorySource& m) : m_(m) {}
    bool write(uint64_t off, const void* b, size_t n) override {
        auto& d = m_.bytes(); if (off > d.size() || n > d.size() - off) return false;
        std::memcpy(d.data() + off, b, n); ++writes; lo = std::min(lo, off); hi = std::max(hi, off + n); return true;
    }
    bool flush() override { return true; }
    int writes = 0; uint64_t lo = ~0ull, hi = 0;
private:
    MemorySource& m_;
};

static uint32_t g32(const std::vector<uint8_t>& d, size_t o) { return d[o] | d[o + 1] << 8 | d[o + 2] << 16 | uint32_t(d[o + 3]) << 24; }

static void scenario(uint32_t ss, uint64_t diskMiB, uint64_t partMiB) {
    MemorySource m(std::vector<uint8_t>(diskMiB << 20, 0));
    TargetFacts t; t.number = 5; t.size = m.size(); t.sectorSize = ss; t.isVirtual = true;
    const uint64_t A = alignSectors(ss);
    { PartOp i; i.kind = PartOp::Kind::InitGpt; auto pl = planPartOp(m, t, i); MemSink s(m); CHECK(applyPartOp(pl, pl.token, s, m, ss).ok); }
    { PartOp c; c.kind = PartOp::Kind::Create; c.firstLba = A; c.sectorCount = (partMiB << 20) / ss; c.type = "fat32";
      auto pl = planPartOp(m, t, c); MemSink s(m); CHECK(applyPartOp(pl, pl.token, s, m, ss).ok); }
    // junk in the partition that formatting must not need to leave behind in the metadata areas
    for (size_t i = 0; i < 64 * 1024; ++i) m.bytes()[A * ss + 3 * ss + i] = 0xAB;
    FormatOp f; f.index = 1; f.label = "datos_1";
    auto plan = planFormat(m, t, f);
    CHECK(plan.allowed && plan.token == "FORMAT DISK 5 PARTITION 1");
    MemSink s(m);
    CHECK(!applyFormat(plan, "x", s, m).ok && s.writes == 0);
    auto r = applyFormat(plan, plan.token, s, m);
    CHECK(r.ok);
    CHECK(s.lo >= A * ss && s.hi <= A * ss + (partMiB << 20));   // never outside the partition
    auto l = analyze(m, ss);
    CHECK(!l.hasErrors() && l.partitions.size() == 1 && l.partitions[0].filesystem == "FAT32");
    std::vector<uint8_t> b(ss); m.read(A * ss, b.data(), ss);
    CHECK(g32(b, 28) == A && g32(b, 32) == (partMiB << 20) / ss && std::memcmp(&b[71], "DATOS_1    ", 11) == 0);
    uint32_t fatSz = g32(b, 36), spc = b[13];
    CHECK(fatSz > 0 && (spc == 1 || spc == 8) && b[16] == 2 && g32(b, 44) == 2);
    // clusters computed from the BPB must be >= 65525 and the FAT must hold them all
    uint64_t data = g32(b, 32) - 32 - 2ull * fatSz; uint64_t clusters = data / spc;
    CHECK(clusters >= 65525 && uint64_t(fatSz) * (ss / 4) >= clusters + 2);
    std::vector<uint8_t> fat(ss); m.read(A * ss + 32ull * ss, fat.data(), ss);
    CHECK(g32(fat, 0) == 0x0FFFFFF8 && g32(fat, 4) == 0x0FFFFFFF && g32(fat, 8) == 0x0FFFFFFF && g32(fat, 12) == 0);
    // junk inside the FAT area is gone (the whole FAT is zeroed except the first entries)
    std::vector<uint8_t> chk(size_t(fatSz) * ss); m.read(A * ss + 32ull * ss, chk.data(), chk.size());
    bool clean = true; for (size_t i = 12; i < chk.size(); ++i) clean &= chk[i] == 0; CHECK(clean);
    std::vector<uint8_t> rd(32); m.read(A * ss + (32ull + 2ull * fatSz) * ss, rd.data(), 32);
    CHECK(std::memcmp(rd.data(), "DATOS_1    ", 11) == 0 && rd[11] == 0x08);
    // backup boot sector identical
    std::vector<uint8_t> bk(ss); m.read(A * ss + 6ull * ss, bk.data(), ss); CHECK(bk == b);
}

static uint64_t g64(const std::vector<uint8_t>& d, size_t o) { return g32(d, o) | uint64_t(g32(d, o + 4)) << 32; }
static uint32_t ck(uint32_t c, uint8_t b) { return ((c << 31) | (c >> 1)) + b; }

static void exfatScenario(uint32_t ss, uint64_t diskMiB, uint64_t partMiB) {
    MemorySource m(std::vector<uint8_t>(diskMiB << 20, 0));
    TargetFacts t; t.number = 5; t.size = m.size(); t.sectorSize = ss; t.isVirtual = true;
    const uint64_t A = alignSectors(ss);
    { PartOp i; i.kind = PartOp::Kind::InitGpt; auto pl = planPartOp(m, t, i); MemSink s(m); CHECK(applyPartOp(pl, pl.token, s, m, ss).ok); }
    { PartOp c; c.kind = PartOp::Kind::Create; c.firstLba = A; c.sectorCount = (partMiB << 20) / ss; auto pl = planPartOp(m, t, c); MemSink s(m); CHECK(applyPartOp(pl, pl.token, s, m, ss).ok); }
    FormatOp f; f.index = 1; f.fs = "exfat"; f.label = "exdata";
    auto plan = planFormat(m, t, f);
    CHECK(plan.allowed);
    MemSink s(m);
    CHECK(applyFormat(plan, plan.token, s, m).ok);
    CHECK(s.lo >= A * ss && s.hi <= A * ss + (partMiB << 20));
    auto l = analyze(m, ss);
    CHECK(!l.hasErrors() && l.partitions[0].filesystem == "exFAT");
    std::vector<uint8_t> r(size_t(12) * ss); m.read(A * ss, r.data(), r.size());
    CHECK(g64(r, 64) == A && g64(r, 72) == (partMiB << 20) / ss && r[108] == (ss == 512 ? 9 : 12) && r[110] == 1);
    uint32_t fatOff = g32(r, 80), fatLen = g32(r, 84), heap = g32(r, 88), clusters = g32(r, 92), root = g32(r, 96);
    CHECK(fatOff >= 24 && heap >= fatOff + fatLen && uint64_t(fatLen) * ss >= uint64_t(clusters + 2) * 4);
    CHECK(uint64_t(heap) + uint64_t(clusters) * (4096 / ss) <= (partMiB << 20) / ss);
    uint32_t sum = 0; for (size_t i = 0; i < size_t(11) * ss; ++i) { if (i == 106 || i == 107 || i == 112) continue; sum = ck(sum, r[i]); }
    bool allSum = true; for (size_t i = 0; i < ss; i += 4) allSum &= g32(r, size_t(11) * ss + i) == sum; CHECK(allSum);
    std::vector<uint8_t> bk(size_t(12) * ss); m.read(A * ss + 12ull * ss, bk.data(), bk.size()); CHECK(bk == r);
    std::vector<uint8_t> fat(size_t(fatLen) * ss); m.read((A + fatOff) * ss, fat.data(), fat.size());
    CHECK(g32(fat, 0) == 0xFFFFFFF8 && g32(fat, 4) == 0xFFFFFFFF && g32(fat, 8 + 4 * 0) != 0);
    // root directory: label, bitmap, up-case entries; the up-case checksum matches the table
    std::vector<uint8_t> rd(96); m.read((A + heap + uint64_t(root - 2) * (4096 / ss)) * ss, rd.data(), 96);
    CHECK(rd[0] == 0x83 && rd[1] == 6 && rd[2] == 'E' && rd[4] == 'X' && rd[32] == 0x81 && rd[64] == 0x82);
    uint32_t upCl = g32(rd, 64 + 20); uint64_t upLen = g64(rd, 64 + 24);
    std::vector<uint8_t> up(static_cast<size_t>(upLen), uint8_t(0)); m.read((A + heap + uint64_t(upCl - 2) * (4096 / ss)) * ss, up.data(), up.size());
    uint32_t us = 0; for (uint8_t b : up) us = ck(us, b);
    CHECK(us == g32(rd, 64 + 4));
    // bitmap marks exactly the used clusters (bitmap + up-case + root) and nothing else
    uint32_t bmBytes = uint32_t(g64(rd, 32 + 24)); std::vector<uint8_t> bm(static_cast<size_t>(bmBytes), uint8_t(0)); m.read((A + heap) * ss, bm.data(), bm.size());
    uint32_t setBits = 0; for (uint8_t b : bm) for (int k = 0; k < 8; ++k) setBits += (b >> k) & 1;
    CHECK(setBits == root - 2 + 1);
}
int main() {
    scenario(512, 64, 40);       // small partition: 512-byte clusters
    scenario(512, 512, 400);     // larger: 4 KiB clusters
    scenario(4096, 512, 400);    // 4096-byte sectors
    exfatScenario(512, 64, 40); exfatScenario(512, 512, 400); exfatScenario(4096, 512, 400);

    MemorySource m(std::vector<uint8_t>(64ull << 20, 0));
    TargetFacts t; t.number = 2; t.size = m.size(); t.sectorSize = 512; t.isVirtual = true;
    { PartOp i; i.kind = PartOp::Kind::InitMbr; auto pl = planPartOp(m, t, i); MemSink s(m); CHECK(applyPartOp(pl, pl.token, s, m, 512).ok); }
    { PartOp c; c.kind = PartOp::Kind::Create; c.firstLba = 2048; c.sectorCount = 20 * 2048; auto pl = planPartOp(m, t, c); MemSink s(m); CHECK(applyPartOp(pl, pl.token, s, m, 512).ok); }
    FormatOp f; f.index = 1;
    CHECK(!planFormat(m, t, f).allowed);                         // 20 MiB: too small for FAT32
    f.index = 9; CHECK(!planFormat(m, t, f).allowed);
    f.index = 1; f.fs = "ntfs"; CHECK(!planFormat(m, t, f).allowed);
    f.fs = "fat32"; f.label = "bad*label"; CHECK(!planFormat(m, t, f).allowed);
    f.label = "THIS LABEL IS TOO LONG"; CHECK(!planFormat(m, t, f).allowed);
    TargetFacts sys = t; sys.isSystemDisk = true; f.label = ""; CHECK(!planFormat(m, sys, f).allowed);
    TargetFacts phys = t; phys.isVirtual = false; CHECK(!planFormat(m, phys, f).allowed);
    TargetFacts big = t; big.size = kMaxWritableDisk + 1; CHECK(!planFormat(m, big, f).allowed);
    for (auto b : m.bytes()) (void)b;
    // tampered plan: a write outside the partition is refused and nothing is written
    MemorySource m2(std::vector<uint8_t>(512ull << 20, 0)); TargetFacts t2 = t; t2.size = m2.size();
    { PartOp i; i.kind = PartOp::Kind::InitGpt; auto pl = planPartOp(m2, t2, i); MemSink s(m2); CHECK(applyPartOp(pl, pl.token, s, m2, 512).ok); }
    { PartOp c; c.kind = PartOp::Kind::Create; c.firstLba = 2048; c.sectorCount = 100 * 2048; auto pl = planPartOp(m2, t2, c); MemSink s(m2); CHECK(applyPartOp(pl, pl.token, s, m2, 512).ok); }
    FormatOp g; g.index = 1; auto plan = planFormat(m2, t2, g); CHECK(plan.allowed);
    MemSink s2(m2);
    auto bad = plan; bad.writes.push_back({0, std::vector<uint8_t>(512, 1)});
    CHECK(!applyFormat(bad, bad.token, s2, m2).ok && s2.writes == 0);
    struct Liar : ByteSink { bool write(uint64_t, const void*, size_t) override { return true; } bool flush() override { return true; } } liar;
    CHECK(!applyFormat(plan, plan.token, liar, m2).ok);
    if (!failures) std::printf("format: all passed\n");
    return failures ? 1 : 0;
}