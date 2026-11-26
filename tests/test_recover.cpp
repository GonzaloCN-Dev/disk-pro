// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
#include <atomic>
#include <cstdio>
#include <cstring>
#include "format.h"
#include "recover.h"

using namespace diskpro;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

class MemSink final : public ByteSink {
public:
    explicit MemSink(MemorySource& m) : m_(m) {}
    bool write(uint64_t off, const void* b, size_t n) override { auto& d = m_.bytes(); if (off > d.size() || n > d.size() - off) return false; std::memcpy(d.data() + off, b, n); return true; }
    bool flush() override { return true; }
private:
    MemorySource& m_;
};

static void scenario(uint32_t ss, bool gpt) {
    MemorySource m(std::vector<uint8_t>(512ull << 20, 0));
    TargetFacts t; t.number = 3; t.size = m.size(); t.sectorSize = ss; t.isVirtual = true;
    auto run = [&](const PartOp& o) { auto pl = planPartOp(m, t, o); MemSink s(m); return pl.allowed && applyPartOp(pl, pl.token, s, m, ss).ok; };
    auto fmt = [&](int idx, const char* fs) { FormatOp f; f.index = idx; f.fs = fs; auto pl = planFormat(m, t, f); MemSink s(m); return pl.allowed && applyFormat(pl, pl.token, s, m).ok; };
    const uint64_t MiB = 1048576 / ss;
    PartOp i; i.kind = gpt ? PartOp::Kind::InitGpt : PartOp::Kind::InitMbr; CHECK(run(i));
    PartOp c; c.kind = PartOp::Kind::Create; c.type = "ntfs";
    c.firstLba = 1 * MiB; c.sectorCount = 100 * MiB; CHECK(run(c));
    c.firstLba = 110 * MiB; c.sectorCount = 150 * MiB; CHECK(run(c));
    const char* fs1 = ss == 512 ? "fat32" : "exfat";   // FAT32 needs >= 65525 clusters: too big for 100 MiB with 4096-byte sectors
    const char* name1 = ss == 512 ? "FAT32" : "exFAT";
    CHECK(fmt(1, fs1)); CHECK(fmt(2, "exfat"));
    std::atomic<bool> cancel{false};
    // intact disk: both volumes found, none "lost" (they overlap the table)
    auto r0 = findLostPartitions(m, ss, cancel);
    CHECK(r0.ok && r0.found.size() == 2 && r0.found[0].overlapsExisting && r0.found[1].overlapsExisting);
    // wipe the table: recovery must find both exactly
    PartOp wipe; wipe.kind = gpt ? PartOp::Kind::InitGpt : PartOp::Kind::InitMbr; CHECK(run(wipe));
    auto r = findLostPartitions(m, ss, cancel);
    CHECK(r.ok && r.found.size() == 2);
    if (r.found.size() == 2) {
        CHECK(r.found[0].filesystem == name1 && r.found[0].firstLba == 1 * MiB && r.found[0].sectorCount == 100 * MiB && !r.found[0].overlapsExisting);
        CHECK(r.found[1].filesystem == "exFAT" && r.found[1].firstLba == 110 * MiB && r.found[1].sectorCount == 150 * MiB);
        // recreating the entries restores a table that analyze() sees with the right file systems
        for (auto& f : r.found) { PartOp q; q.kind = PartOp::Kind::Create; q.type = "ntfs"; q.firstLba = f.firstLba; q.sectorCount = f.sectorCount; CHECK(run(q)); }
        auto l = analyze(m, ss);
        CHECK(!l.hasErrors() && l.partitions.size() == 2 && l.partitions[0].filesystem == name1 && l.partitions[1].filesystem == "exFAT");
    }
    // cancel
    std::atomic<bool> stop{true};
    CHECK(findLostPartitions(m, ss, stop).cancelled);
}

int main() {
    scenario(512, false); scenario(512, true); scenario(4096, false); scenario(4096, true);
    // empty / garbage disk finds nothing
    MemorySource z(std::vector<uint8_t>(16ull << 20, 0)); std::atomic<bool> cancel{false};
    auto r = findLostPartitions(z, 512, cancel); CHECK(r.ok && r.found.empty());
    for (size_t k = 0; k < z.bytes().size(); ++k) z.bytes()[k] = uint8_t(k * 31 + (k >> 8));
    r = findLostPartitions(z, 512, cancel); CHECK(r.ok && r.found.empty());
    CHECK(!findLostPartitions(z, 1000, cancel).ok);
    if (!failures) std::printf("recover: all passed\n");
    return failures ? 1 : 0;
}