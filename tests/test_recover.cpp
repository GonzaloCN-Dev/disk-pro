// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include "format.h"
#include "recover.h"

using namespace diskpro;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)
static void w16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
static void w32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i)); }

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
        CHECK(r.found[0].confidence >= 80 && !r.found[0].evidence.empty());
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

static void deletedFiles() {
    constexpr uint32_t ss = 512, total = 131072, reserved = 32, fatSectors = 1009;
    MemorySource volume(std::vector<uint8_t>(uint64_t(total) * ss, 0));
    auto& b = volume.bytes();
    b[0] = 0xEB; std::memcpy(&b[3], "DISKPRO ", 8); w16(&b[11], ss); b[13] = 1;
    w16(&b[14], reserved); b[16] = 2; w32(&b[32], total); w32(&b[36], fatSectors);
    w32(&b[44], 2); std::memcpy(&b[82], "FAT32   ", 8); b[510] = 0x55; b[511] = 0xAA;
    const uint64_t fat = uint64_t(reserved) * ss;
    w32(&b[fat], 0x0FFFFFF8); w32(&b[fat + 4], 0x0FFFFFFF); w32(&b[fat + 8], 0x0FFFFFFF);
    // Candidate 2 keeps a non-contiguous intact chain 5 -> 7.
    w32(&b[fat + 5 * 4], 7); w32(&b[fat + 7 * 4], 0x0FFFFFFF);
    const uint64_t data = uint64_t(reserved + 2 * fatSectors) * ss;
    uint8_t* e = &b[data];
    e[0] = 0xE5; std::memcpy(e + 1, "ELLO   TXT", 10); e[11] = 0x20; w16(e + 26, 3); w32(e + 28, 700);
    e += 32;
    e[0] = 0xE5; std::memcpy(e + 1, "HAIN   BIN", 10); e[11] = 0x20; w16(e + 26, 5); w32(e + 28, 700);
    for (size_t i = 0; i < 700; ++i) {
        b[data + ss + i] = uint8_t(i * 3);
        const uint64_t chainOff = i < ss ? data + 3ull * ss + i : data + 5ull * ss + (i - ss);
        b[chainOff] = uint8_t(i * 7);
    }
    std::atomic<bool> cancel{false};
    auto scan = findDeletedFat32Files(volume, cancel);
    CHECK(scan.ok && scan.files.size() == 2);
    if (scan.files.size() == 2) {
        CHECK(scan.files[0].path == "_ELLO.TXT" && scan.files[0].confidence == 65);
        CHECK(scan.files[1].path == "_HAIN.BIN" && scan.files[1].confidence == 85);
        for (const auto& file : scan.files) {
            std::vector<uint8_t> out;
            auto recovered = recoverFat32File(volume, file, cancel, [&](const void* p, size_t n) {
                const auto* q = static_cast<const uint8_t*>(p); out.insert(out.end(), q, q + n); return true;
            });
            CHECK(recovered.ok && recovered.bytes == 700 && recovered.sha256.size() == 64 && out.size() == 700);
            if (out.size() == 700) {
                const int multiplier = file.firstCluster == 3 ? 3 : 7;
                for (size_t i = 0; i < out.size(); ++i) CHECK(out[i] == uint8_t(i * multiplier));
            }
        }
        const auto outPath = std::filesystem::temp_directory_path() / "diskpro-fat32-recovery-test.bin";
        std::error_code ec;
        std::filesystem::remove(outPath, ec);
        auto saved = recoverFat32FileToNewFile(volume, scan.files[0], outPath.string(), cancel);
        CHECK(saved.ok && std::filesystem::file_size(outPath, ec) == 700);
        auto noOverwrite = recoverFat32FileToNewFile(volume, scan.files[0], outPath.string(), cancel);
        CHECK(!noOverwrite.ok && std::filesystem::file_size(outPath, ec) == 700);
        std::filesystem::remove(outPath, ec);
        auto changed = scan.files[0];
        b[changed.directoryEntryOffset + 28] ^= 1;
        auto rejected = recoverFat32File(volume, changed, cancel, [](const void*, size_t) { return true; });
        CHECK(!rejected.ok && rejected.bytes == 0);
        b[changed.directoryEntryOffset + 28] ^= 1;
        b[changed.directoryEntryOffset] = 'H'; // a live directory entry now owns the old candidate clusters
        size_t unexpectedWrites = 0;
        auto reused = recoverFat32File(volume, scan.files[0], cancel, [&](const void*, size_t) {
            ++unexpectedWrites; return true;
        });
        CHECK(!reused.ok && reused.bytes == 0 && unexpectedWrites == 0);
        b[changed.directoryEntryOffset] = 0xE5;

        const auto sourcePath = std::filesystem::temp_directory_path() / "diskpro-fat32-source-image-test.raw";
        {
            std::ofstream image(sourcePath, std::ios::binary | std::ios::trunc);
            image.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
            CHECK(bool(image));
        }
        {
            FileSource image(sourcePath.string());
            CHECK(image.ok());
            auto fresh = findDeletedFat32Files(image, cancel);
            CHECK(fresh.ok && !fresh.files.empty());
            if (fresh.ok && !fresh.files.empty()) {
                auto sameSource = recoverFat32FileToNewFile(image, fresh.files[0], sourcePath.string(), cancel);
                CHECK(!sameSource.ok && sameSource.error.find("source image") != std::string::npos);
            }
        }
        std::filesystem::remove(sourcePath, ec);
    }
    std::atomic<bool> stop{true};
    CHECK(findDeletedFat32Files(volume, stop).cancelled);
}

int main() {
    scenario(512, false); scenario(512, true); scenario(4096, false); scenario(4096, true);
    deletedFiles();
    // empty / garbage disk finds nothing
    MemorySource z(std::vector<uint8_t>(16ull << 20, 0)); std::atomic<bool> cancel{false};
    auto r = findLostPartitions(z, 512, cancel); CHECK(r.ok && r.found.empty());
    for (size_t k = 0; k < z.bytes().size(); ++k) z.bytes()[k] = uint8_t(k * 31 + (k >> 8));
    r = findLostPartitions(z, 512, cancel); CHECK(r.ok && r.found.empty());
    CHECK(!findLostPartitions(z, 1000, cancel).ok);
    if (!failures) std::printf("recover: all passed\n");
    return failures ? 1 : 0;
}
