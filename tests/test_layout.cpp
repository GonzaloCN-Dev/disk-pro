// Synthetic in-memory images only. No real disks are touched.
#include <cstdio>
#include <cstring>

#include "layout.h"

using namespace diskpro;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static void p32(std::vector<uint8_t>& d, size_t o, uint32_t v) { for (int i = 0; i < 4; ++i) d[o + i] = uint8_t(v >> (8 * i)); }
static void p64(std::vector<uint8_t>& d, size_t o, uint64_t v) { p32(d, o, uint32_t(v)); p32(d, o + 4, uint32_t(v >> 32)); }

static void mbrEntry(std::vector<uint8_t>& d, size_t base, int i, uint8_t boot, uint8_t type, uint32_t lba, uint32_t n) {
    size_t e = base + 446 + i * 16;
    d[e] = boot; d[e + 4] = type; p32(d, e + 8, lba); p32(d, e + 12, n);
    d[base + 510] = 0x55; d[base + 511] = 0xAA;
}

static void gptEntry(std::vector<uint8_t>& d, size_t o, uint8_t typeFirst, uint64_t f, uint64_t l, const char* name) {
    static const uint8_t efi[16] = {0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11, 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B};
    static const uint8_t basic[16] = {0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44, 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7};
    std::memcpy(&d[o], typeFirst == 0x28 ? efi : basic, 16);
    d[o + 16] = 0x77;
    p64(d, o + 32, f); p64(d, o + 40, l);
    for (size_t i = 0; name[i]; ++i) d[o + 56 + i * 2] = uint8_t(name[i]);
}

static void gptHeader(std::vector<uint8_t>& d, uint32_t ss, uint64_t my, uint64_t alt, uint64_t entriesLba, uint32_t entriesCrc, uint64_t total) {
    size_t o = my * ss;
    std::memcpy(&d[o], "EFI PART", 8);
    p32(d, o + 8, 0x00010000); p32(d, o + 12, 92);
    p64(d, o + 24, my); p64(d, o + 32, alt);
    p64(d, o + 40, 34); p64(d, o + 48, total - 34);
    d[o + 56] = 0xAB;
    p64(d, o + 72, entriesLba); p32(d, o + 80, 128); p32(d, o + 84, 128); p32(d, o + 88, entriesCrc);
    p32(d, o + 16, 0);
    p32(d, o + 16, crc32(&d[o], 92));
}

// Builds a valid GPT with two partitions for the given sector size.
static std::vector<uint8_t> makeGpt(uint32_t ss, uint64_t total) {
    std::vector<uint8_t> d(total * ss);
    mbrEntry(d, 0, 0, 0, 0xEE, 1, uint32_t(total - 1));
    const uint32_t tableBytes = 128 * 128;
    uint64_t tableSectors = (tableBytes + ss - 1) / ss;
    uint64_t primaryTable = 2, backupTable = total - 1 - tableSectors;
    gptEntry(d, primaryTable * ss, 0x28, 2048, 4095, "EFI");
    gptEntry(d, primaryTable * ss + 128, 0xA2, 4096, 8191, "Data");
    std::memcpy(&d[backupTable * ss], &d[primaryTable * ss], tableBytes);
    uint32_t tcrc = crc32(&d[primaryTable * ss], tableBytes);
    gptHeader(d, ss, 1, total - 1, primaryTable, tcrc, total);
    gptHeader(d, ss, total - 1, 1, backupTable, tcrc, total);
    // NTFS marker in partition 2
    size_t b = 4096 * ss;
    std::memcpy(&d[b + 3], "NTFS    ", 8); d[b + 510] = 0x55; d[b + 511] = 0xAA;
    return d;
}

static void testGpt(uint32_t ss) {
    uint64_t total = ss == 512 ? 16384 : 4096;
    if (ss == 4096) {  // keep partitions inside the smaller disk
        total = 16384;
    }
    MemorySource s(makeGpt(ss, total));
    DiskLayout l = analyze(s);
    CHECK(l.scheme == Scheme::GPT);
    CHECK(l.sectorSize == ss);
    CHECK(!l.hasErrors());
    CHECK(l.partitions.size() == 2);
    if (l.partitions.size() == 2) {
        CHECK(l.partitions[0].typeName == "EFI System");
        CHECK(l.partitions[0].name == "EFI");
        CHECK(l.partitions[1].filesystem == "NTFS");
        CHECK(l.partitions[1].sectorCount == 4096);
    }
}

static void testGptCorruption() {
    auto d = makeGpt(512, 16384);
    d[2 * 512 + 40] ^= 0xFF;  // damage entry array
    DiskLayout l = analyze(MemorySource(d));
    CHECK(l.hasErrors());
    auto t = makeGpt(512, 16384);
    t.resize(t.size() / 2);  // truncated image
    DiskLayout l2 = analyze(MemorySource(t));
    CHECK(l2.hasErrors());
}

static void testMbr() {
    std::vector<uint8_t> d(8192 * 512);
    mbrEntry(d, 0, 0, 0x80, 0x07, 2048, 2048);
    mbrEntry(d, 0, 1, 0, 0x0C, 4096, 1024);
    std::memcpy(&d[2048 * 512 + 3], "EXFAT   ", 8); d[2048 * 512 + 510] = 0x55; d[2048 * 512 + 511] = 0xAA;
    DiskLayout l = analyze(MemorySource(d));
    CHECK(l.scheme == Scheme::MBR);
    CHECK(!l.hasErrors());
    CHECK(l.partitions.size() == 2);
    CHECK(l.partitions[0].bootable);
    CHECK(l.partitions[0].filesystem == "exFAT");
}

static void testMbrOverlapAndRange() {
    std::vector<uint8_t> d(8192 * 512);
    mbrEntry(d, 0, 0, 0, 0x07, 2048, 2048);
    mbrEntry(d, 0, 1, 0, 0x07, 3000, 2048);
    CHECK(analyze(MemorySource(d)).hasErrors());
    std::vector<uint8_t> e(8192 * 512);
    mbrEntry(e, 0, 0, 0, 0x07, 8000, 5000);
    CHECK(analyze(MemorySource(e)).hasErrors());
}

static void testExtendedLoop() {
    std::vector<uint8_t> d(8192 * 512);
    mbrEntry(d, 0, 0, 0, 0x05, 100, 4000);
    // EBR at 100 points to itself (next offset 0 -> stops) and to a backwards link
    mbrEntry(d, 100 * 512, 0, 0, 0x07, 1, 50);
    mbrEntry(d, 100 * 512, 1, 0, 0x05, 0, 0);
    DiskLayout l = analyze(MemorySource(d));
    CHECK(l.partitions.size() == 2);  // extended + one logical
    CHECK(l.partitions.size() == 2 && l.partitions[1].logical);
    mbrEntry(d, 100 * 512, 1, 0, 0x05, 0, 10);  // next = base+0 -> loop back to ebr? base+0 == 100 == ebr
    analyze(MemorySource(d));                    // must terminate
}

static void testRawAndTiny() {
    CHECK(analyze(MemorySource(std::vector<uint8_t>(1024 * 512))).scheme == Scheme::Raw);
    CHECK(analyze(MemorySource(std::vector<uint8_t>(100))).hasErrors());
}

static void testFixedVhdFooter() {
    auto d = makeGpt(512, 16384);
    d.resize(d.size() + 512);
    std::memcpy(&d[d.size() - 512], "conectix", 8);
    DiskLayout l = analyze(MemorySource(d));
    CHECK(l.scheme == Scheme::GPT);
    CHECK(l.diagnostics.empty());
    CHECK(l.totalSectors == 16384);
}

static void testIoErrors() {
    struct Failing final : ByteSource {
        MemorySource inner;
        uint64_t failFrom;
        Failing(std::vector<uint8_t> d, uint64_t f) : inner(std::move(d)), failFrom(f) {}
        uint64_t size() const override { return inner.size(); }
        bool read(uint64_t o, void* b, size_t n) const override { return o >= failFrom ? false : inner.read(o, b, n); }
    };
    CHECK(analyze(Failing(makeGpt(512, 16384), 0)).hasErrors());           // sector 0 unreadable
    CHECK(analyze(Failing(makeGpt(512, 16384), 2 * 512)).hasErrors());     // entry array unreadable
}

static void testCrc() {
    CHECK(crc32("123456789", 9) == 0xCBF43926u);
    CHECK(humanSize(1536) == "1.50 KiB");
}

#include "map.h"

static void testMap() {
    const uint64_t MiB = 1024 * 1024;
    auto r = buildMap(100 * MiB, 4096, {{1, MiB, 40 * MiB}, {2, 41 * MiB, 40 * MiB}});
    CHECK(r.diagnostics.empty());
    CHECK(r.segments.size() == 3);  // 2 partitions + trailing free
    if (r.segments.size() == 3) CHECK(r.segments[2].free);
    double sum = 0; for (auto& s : r.segments) sum += s.fraction;
    CHECK(sum > 0.999 && sum < 1.001);
    CHECK(buildMap(100 * MiB, 4096, {{1, MiB, 60 * MiB}, {2, 30 * MiB, 20 * MiB}}).diagnostics.size() >= 1);   // overlap
    CHECK(buildMap(100 * MiB, 4096, {{1, 90 * MiB, 20 * MiB}}).diagnostics.size() == 1);                       // out of range
    CHECK(buildMap(100 * MiB, 4096, {{1, 512, 10 * MiB}}).diagnostics.size() == 1);                           // misaligned
    CHECK(buildMap(0, 512, {}).diagnostics.size() == 1);
}

int main() {
    testMap();
    testGpt(512); testGpt(4096);
    testGptCorruption(); testMbr(); testMbrOverlapAndRange();
    testExtendedLoop(); testRawAndTiny(); testCrc(); testFixedVhdFooter(); testIoErrors();
    std::printf(failures ? "%d FAILURES\n" : "all tests passed\n", failures);
        {   // RangeSource: a window over another source, never reads outside it
        std::vector<uint8_t> raw(4096); for (size_t i = 0; i < raw.size(); ++i) raw[i] = uint8_t(i & 0xFF);
        auto base = std::make_shared<MemorySource>(raw);
        RangeSource w(base, 1000, 500);
        uint8_t b[16] = {};
        CHECK(w.size() == 500);
        CHECK(w.read(0, b, 16) && b[0] == uint8_t(1000 & 0xFF) && b[15] == uint8_t(1015 & 0xFF));
        CHECK(w.read(484, b, 16));                 // last 16 bytes
        CHECK(!w.read(485, b, 16));                // would cross the end
        CHECK(!w.read(501, b, 1));
        RangeSource clamped(base, 4000, 5000);     // longer than the base: clamped
        CHECK(clamped.size() == 96);
        RangeSource beyond(base, 9000, 10);        // starts outside: empty
        CHECK(beyond.size() == 0 && !beyond.read(0, b, 1));
    }return failures ? 1 : 0;
}
