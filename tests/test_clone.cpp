#include <cstdio>
#include <cstring>
#include "clone.h"
using namespace diskpro;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

struct MemSink final : ByteSink {
    MemorySource& m; uint64_t failAfter = ~0ull, writes = 0; bool corrupt = false, canPrepare = true;
    explicit MemSink(MemorySource& s) : m(s) {}
    bool write(uint64_t off, const void* b, size_t n) override {
        if (writes++ >= failAfter) return false;
        if (off + n > m.bytes().size()) return false;
        std::memcpy(m.bytes().data() + off, b, n);
        if (corrupt) m.bytes()[off] ^= 0xFF;
        return true;
    }
    bool flush() override { return true; }
    bool prepare(uint64_t, uint64_t) override { return canPrepare; }
};

static std::vector<uint8_t> pattern(size_t n) { std::vector<uint8_t> v(n); for (size_t i = 0; i < n; ++i) v[i] = uint8_t((i * 31 + (i >> 8)) & 0xFF); return v; }
static TargetFacts target(uint64_t size) { TargetFacts t; t.number = 5; t.size = size; t.isVirtual = true; return t; }

int main() {
    const size_t MB = 1u << 20;
    CloneSourceFacts s; s.size = 3 * MB + 512 * 7; s.sectorSize = 512;
    CHECK(planClone(s, target(4 * MB)).allowed);
    CHECK(planClone(s, target(4 * MB)).token == "CLONE TO DISK 5");
    { auto t = target(4 * MB); t.isSystemDisk = true; auto p = planClone(s, t); CHECK(!p.allowed && p.bytes == 0); }
    { auto t = target(4 * MB); t.isVirtual = false; CHECK(!planClone(s, t).allowed); }
    CHECK(!planClone(s, target(2 * MB)).allowed);                       // does not fit
    CHECK(!planClone(s, target(kMaxWritableDisk + 512)).allowed);       // over the 4 GiB limit
    { auto t = target(4 * MB); t.sectorSize = 4096; CHECK(!planClone(s, t).allowed); }
    { auto q = s; q.isTargetItself = true; CHECK(!planClone(q, target(4 * MB)).allowed); }
    { auto q = s; q.size = 0; CHECK(!planClone(q, target(4 * MB)).allowed); }
    { auto q = s; q.size = 1000; CHECK(!planClone(q, target(4 * MB)).allowed); }  // not sector multiple

    auto plan = planClone(s, target(4 * MB));
    MemorySource src(pattern(size_t(s.size)));
    { // target replaced after preview
        MemorySource dm(std::vector<uint8_t>(5 * MB, 0xEE)); MemSink sk(dm);
        auto r = applyClone(src, sk, dm, plan, plan.token, nullptr);
        CHECK(!r.ok && !r.written && sk.writes == 0);
    }

    { // wrong token writes nothing
        MemorySource dm(std::vector<uint8_t>(4 * MB, 0xEE)); MemSink sk(dm);
        auto r = applyClone(src, sk, dm, plan, "clone to disk 5", nullptr);
        CHECK(!r.ok && !r.written && dm.bytes()[0] == 0xEE);
    }
    { // mounted/in-use target writes nothing
        MemorySource dm(std::vector<uint8_t>(4 * MB, 0xEE)); MemSink sk(dm); sk.canPrepare = false;
        auto r = applyClone(src, sk, dm, plan, plan.token, nullptr);
        CHECK(!r.ok && !r.written && sk.writes == 0);
    }
    { // disallowed plan writes nothing
        auto bad = planClone(s, target(2 * MB));
        MemorySource dm(std::vector<uint8_t>(2 * MB, 0xEE)); MemSink sk(dm);
        auto r = applyClone(src, sk, dm, bad, bad.token, nullptr);
        CHECK(!r.ok && !r.written && dm.bytes()[0] == 0xEE);
    }
    { // success, with progress and verification
        MemorySource dm(std::vector<uint8_t>(4 * MB, 0xEE)); MemSink sk(dm);
        uint64_t lastCopy = 0; int verifyCalls = 0; bool monotonic = true;
        auto r = applyClone(src, sk, dm, plan, plan.token, [&](int ph, uint64_t d, uint64_t tot) {
            CHECK(tot == s.size);
            if (ph == 0) { if (d < lastCopy) monotonic = false; lastCopy = d; } else ++verifyCalls;
            return true; });
        CHECK(r.ok && r.written && r.copied == s.size && monotonic && lastCopy == s.size && verifyCalls >= 2);
        CHECK(std::memcmp(dm.bytes().data(), src.bytes().data(), size_t(s.size)) == 0);
        CHECK(dm.bytes()[size_t(s.size)] == 0xEE);                       // nothing beyond the source size
    }
    { // cancel in the middle
        MemorySource dm(std::vector<uint8_t>(4 * MB, 0xEE)); MemSink sk(dm);
        int calls = 0;
        auto r = applyClone(src, sk, dm, plan, plan.token, [&](int, uint64_t, uint64_t) { return ++calls < 3; });
        CHECK(!r.ok && r.cancelled && r.copied == 2 * MB && dm.bytes()[3 * MB] == 0xEE);
    }
    { // write error
        MemorySource dm(std::vector<uint8_t>(4 * MB, 0xEE)); MemSink sk(dm); sk.failAfter = 1;
        auto r = applyClone(src, sk, dm, plan, plan.token, nullptr);
        CHECK(!r.ok && r.error == "write failed");
    }
    { // silent corruption is detected
        MemorySource dm(std::vector<uint8_t>(4 * MB, 0xEE)); MemSink sk(dm); sk.corrupt = true;
        auto r = applyClone(src, sk, dm, plan, plan.token, nullptr);
        CHECK(!r.ok && r.error.find("verification failed") != std::string::npos);
    }
    std::printf(failures ? "clone: %d failures\n" : "clone: all passed\n", failures);
    return failures ? 1 : 0;
}
