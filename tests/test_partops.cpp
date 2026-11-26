// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
#include <cstdio>
#include <cstring>
#include <memory>
#include "partops.h"

using namespace diskpro;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

class MemSink final : public ByteSink {
public:
    explicit MemSink(MemorySource& m) : m_(m) {}
    bool write(uint64_t off, const void* b, size_t n) override {
        auto& d = m_.bytes(); if (off > d.size() || n > d.size() - off) return false;
        std::memcpy(d.data() + off, b, n); ++writes; return true;
    }
    bool flush() override { return true; }
    bool prepare(uint64_t, uint64_t) override { return canPrepare; }
    int writes = 0;
    bool canPrepare = true;
private:
    MemorySource& m_;
};

static TargetFacts facts(const MemorySource& m, uint32_t ss) {
    TargetFacts f; f.number = 7; f.size = m.size(); f.sectorSize = ss; f.isVirtual = true; return f;
}
static bool run(MemorySource& m, uint32_t ss, const PartOp& op, std::string* why = nullptr) {
    auto plan = planPartOp(m, facts(m, ss), op);
    if (!plan.allowed) { if (why && !plan.blockers.empty()) *why = plan.blockers[0]; return false; }
    MemSink s(m);
    auto r = applyPartOp(plan, plan.token, s, m, ss);
    if (!r.ok && why) *why = r.error;
    return r.ok;
}

static void scenario(uint32_t ss, bool gpt) {
    const uint64_t SZ = 64ull << 20;
    MemorySource m(std::vector<uint8_t>(SZ, 0));
    const uint64_t A = alignSectors(ss);
    PartOp init; init.kind = gpt ? PartOp::Kind::InitGpt : PartOp::Kind::InitMbr;
    CHECK(run(m, ss, init));
    auto l = analyze(m, ss);
    CHECK(l.scheme == (gpt ? Scheme::GPT : Scheme::MBR) && l.partitions.empty() && !l.hasErrors());

    PartOp c; c.kind = PartOp::Kind::Create; c.firstLba = A; c.sectorCount = 8 * A; c.type = "ntfs"; c.name = "Datos ñ";
    CHECK(run(m, ss, c));
    c.firstLba = 9 * A; c.sectorCount = 8 * A; c.type = "fat32"; c.name = "B";
    CHECK(run(m, ss, c));
    l = analyze(m, ss);
    CHECK(l.partitions.size() == 2 && !l.hasErrors());
    CHECK(l.partitions[0].firstLba == A && l.partitions[0].sectorCount == 8 * A);
    CHECK(l.partitions[1].firstLba == 9 * A);
    if (gpt) CHECK(l.partitions[0].name == "Datos \xC3\xB1");

    std::string why;
    PartOp bad = c; bad.firstLba = 5 * A;                       // overlaps #1
    CHECK(!run(m, ss, bad, &why));
    bad = c; bad.firstLba = 20 * A + 1;                         // unaligned
    CHECK(!run(m, ss, bad));
    bad = c; bad.firstLba = 30 * A; bad.sectorCount = 1000000;  // beyond the disk
    CHECK(!run(m, ss, bad));
    bad = c; bad.firstLba = 30 * A; bad.type = "0x05";          // extended refused
    if (!gpt) CHECK(!run(m, ss, bad));

    PartOp rs; rs.kind = PartOp::Kind::Resize; rs.index = 1; rs.firstLba = A; rs.sectorCount = 4 * A;
    CHECK(run(m, ss, rs));
    CHECK(analyze(m, ss).partitions[0].sectorCount == 4 * A);
    rs.sectorCount = 20 * A;                                    // would overlap #2
    CHECK(!run(m, ss, rs));

    PartOp ty; ty.kind = PartOp::Kind::SetType; ty.index = 2; ty.type = "linux";
    CHECK(run(m, ss, ty));
    ty.type = "zzz"; CHECK(!run(m, ss, ty));

    if (!gpt) {
        PartOp ac; ac.kind = PartOp::Kind::SetActive; ac.index = 2;
        CHECK(run(m, ss, ac));
        CHECK(analyze(m, ss).partitions[1].bootable && !analyze(m, ss).partitions[0].bootable);
    }

    PartOp mg; mg.kind = PartOp::Kind::Merge; mg.index = 1; mg.index2 = 2;
    CHECK(!run(m, ss, mg));                                     // 1 ends at 5A, 2 starts at 9A: not contiguous
    rs.index = 1; rs.sectorCount = 8 * A; CHECK(run(m, ss, rs)); // now adjacent
    CHECK(run(m, ss, mg));
    l = analyze(m, ss);
    CHECK(l.partitions.size() == 1 && l.partitions[0].sectorCount == 16 * A && !l.hasErrors());

    PartOp del; del.kind = PartOp::Kind::Delete; del.index = 1;
    CHECK(run(m, ss, del));
    CHECK(analyze(m, ss).partitions.empty());
    del.index = 3; CHECK(!run(m, ss, del));
}

int main() {
    scenario(512, false); scenario(512, true); scenario(4096, false); scenario(4096, true);

    // guards
    {
        MemorySource m(std::vector<uint8_t>(64ull << 20, 0));
        PartOp init; init.kind = PartOp::Kind::InitMbr;
        TargetFacts f = facts(m, 512);
        f.isSystemDisk = true; CHECK(!planPartOp(m, f, init).allowed);
        f = facts(m, 512); f.isVirtual = false; CHECK(!planPartOp(m, f, init).allowed);
        f = facts(m, 512); auto plan = planPartOp(m, f, init); CHECK(plan.allowed && plan.token == "EDIT DISK 7");
        MemSink s(m);
        MemSink unavailable(m); unavailable.canPrepare = false;
        auto locked = applyPartOp(plan, plan.token, unavailable, m, 512);
        CHECK(!locked.ok && !locked.written && unavailable.writes == 0);
        MemorySource replaced(std::vector<uint8_t>((64ull << 20) + 512, 0)); MemSink replacedSink(replaced);
        auto stale = applyPartOp(plan, plan.token, replacedSink, replaced, 512);
        CHECK(!stale.ok && !stale.written && replacedSink.writes == 0);
        auto r = applyPartOp(plan, "wrong", s, m, 512);
        CHECK(!r.ok && !r.written && s.writes == 0);
        r = applyPartOp(plan, "edit disk 7", s, m, 512); CHECK(!r.ok && s.writes == 0);
        // tampered plan: a write in the middle of the data area is refused
        auto bad = plan; bad.writes.push_back({10ull << 20, std::vector<uint8_t>(512, 1)});
        r = applyPartOp(bad, bad.token, s, m, 512); CHECK(!r.ok && !r.written && s.writes == 0);
        // planning never writes
        for (auto b : m.bytes()) if (b) { CHECK(false); break; }
        // too big
        TargetFacts big = facts(m, 512); big.size = kMaxWritableDisk + 1; CHECK(!planPartOp(m, big, init).allowed);
        // editing a disk without a table
        PartOp c; c.kind = PartOp::Kind::Create; c.firstLba = 2048; c.sectorCount = 2048;
        CHECK(!planPartOp(m, facts(m, 512), c).allowed);
        // lying sink is caught by verification
        struct Liar : ByteSink { bool write(uint64_t, const void*, size_t) override { return true; } bool flush() override { return true; } } liar;
        auto r2 = applyPartOp(plan, plan.token, liar, m, 512); CHECK(!r2.ok);
    }
    // GUID parsing
    {
        uint8_t g[16];
        CHECK(parseGuid("C12A7328-F81F-11D2-BA4B-00A0C93EC93B", g) && g[0] == 0x28 && g[3] == 0xC1 && g[4] == 0x1F && g[8] == 0xBA);
        CHECK(!parseGuid("nope", g));
        CHECK(!parseGuid("C12A7328-F81F-11D2-BA4B-00A0C93EC93G", g));
    }
    // MBR over GPT must leave no GPT behind
    for (uint32_t ss : {512u, 4096u}) {
        MemorySource m(std::vector<uint8_t>(64ull << 20, 0));
        PartOp g; g.kind = PartOp::Kind::InitGpt; CHECK(run(m, ss, g));
        PartOp c; c.kind = PartOp::Kind::Create; c.firstLba = alignSectors(ss); c.sectorCount = alignSectors(ss); CHECK(run(m, ss, c));
        PartOp mb; mb.kind = PartOp::Kind::InitMbr; CHECK(run(m, ss, mb));
        auto l = analyze(m, ss);
        CHECK(l.scheme == Scheme::MBR && l.partitions.empty() && !l.hasErrors());
        uint8_t sig[8]; m.read(ss, sig, 8); CHECK(std::memcmp(sig, "EFI PART", 8) != 0);
        m.read(m.size() - ss, sig, 8); CHECK(std::memcmp(sig, "EFI PART", 8) != 0);
    }
    if (!failures) std::printf("partops: all passed\n");
    return failures ? 1 : 0;
}
