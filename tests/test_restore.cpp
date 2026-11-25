#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include "restore.h"
#include "tools.h"

using namespace diskpro;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static void p32(std::vector<uint8_t>& d, size_t o, uint32_t v) { for (int i = 0; i < 4; ++i) d[o + i] = uint8_t(v >> (8 * i)); }

// Disk image with a valid MBR: one NTFS-like partition. size must be multiple of 512.
static std::vector<uint8_t> makeMbr(size_t size, uint32_t sig) {
    std::vector<uint8_t> d(size, 0);
    p32(d, 440, sig);
    d[446 + 4] = 0x07; p32(d, 446 + 8, 2048); p32(d, 446 + 12, 4096);
    d[510] = 0x55; d[511] = 0xAA;
    return d;
}

static std::string tmpPath(const char* n) { return (std::filesystem::temp_directory_path() / n).string(); }
static void writeFile(const std::string& p, const std::vector<uint8_t>& d) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc); f.write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
}
static std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}};
}

class FailSink final : public ByteSink {
public:
    bool write(uint64_t, const void*, size_t) override { return false; }
    bool flush() override { return true; }
};
class LyingSink final : public ByteSink {   // claims success but writes nothing
public:
    bool write(uint64_t, const void*, size_t) override { return true; }
    bool flush() override { return true; }
};

int main() {
    const size_t SZ = 4 * 1024 * 1024;
    auto orig = makeMbr(SZ, 0x11223344);
    auto srcPath = tmpPath("dp_src.img"), bakPath = tmpPath("dp_bak.ptb"), tgtPath = tmpPath("dp_tgt.img");
    std::filesystem::remove(bakPath);
    writeFile(srcPath, orig);
    { FileSource s(srcPath); CHECK(backupPartitionTable(s, bakPath).ok); }
    auto bak = loadPartitionBackup(bakPath);
    CHECK(bak.ok && bak.scheme == "MBR" && bak.diskSize == SZ && bak.sectorSize == 512 && bak.diskId == "11223344");

    // Target: same size, wiped table, different content in the data area that must NOT change.
    std::vector<uint8_t> tgt(SZ, 0);
    for (size_t i = 1048576; i < SZ - 1048576; ++i) tgt[i] = uint8_t(i * 7);
    writeFile(tgtPath, tgt);
    TargetFacts f; f.number = 9; f.size = SZ; f.sectorSize = 512; f.isVirtual = true; f.currentScheme = "RAW";

    auto plan = planRestore(bak, f);
    CHECK(plan.allowed && plan.token == "RESTORE DISK 9" && plan.ranges.size() == 2);

    // Safety rules
    { auto t = f; t.isSystemDisk = true; CHECK(!planRestore(bak, t).allowed); }
    { auto t = f; t.isVirtual = false; CHECK(!planRestore(bak, t).allowed); }
    { auto t = f; t.size = SZ + 512; CHECK(!planRestore(bak, t).allowed); }
    { auto t = f; t.sectorSize = 4096; CHECK(!planRestore(bak, t).allowed); }
    { auto t = f; t.size = 0; CHECK(!planRestore(bak, t).allowed); }
    { BackupData bad; bad.error = "x"; CHECK(!planRestore(bad, f).allowed); }
    { auto big = bak; big.diskSize = kMaxWritableDisk + 512; auto t = f; t.size = big.diskSize; CHECK(!planRestore(big, t).allowed); }

    // Nothing is written without the exact typed confirmation or with a disallowed plan.
    {
        FileSource rb(tgtPath); FileSink sk(tgtPath); CHECK(sk.ok());
        auto r = applyRestore(bak, plan, "restore disk 9", sk, rb);
        CHECK(!r.ok && !r.written);
        r = applyRestore(bak, plan, "", sk, rb); CHECK(!r.ok && !r.written);
        auto t = f; t.isSystemDisk = true; auto denied = planRestore(bak, t);
        r = applyRestore(bak, denied, denied.token, sk, rb); CHECK(!r.ok && !r.written);
        RestorePlan forged = plan; forged.ranges[0].length = 512;   // plan tampered: write must refuse
        r = applyRestore(bak, forged, plan.token, sk, rb); CHECK(!r.ok && !r.written);
    }
    CHECK(readFile(tgtPath) == tgt);

    // Real restore
    {
        FileSource rb(tgtPath); FileSink sk(tgtPath);
        auto r = applyRestore(bak, plan, plan.token, sk, rb);
        CHECK(r.ok && r.written);
    }
    auto after = readFile(tgtPath);
    CHECK(after.size() == SZ);
    CHECK(std::memcmp(after.data(), orig.data(), 1048576) == 0);                       // head restored
    CHECK(std::memcmp(after.data() + SZ - 1048576, orig.data() + SZ - 1048576, 1048576) == 0);  // tail restored
    CHECK(std::memcmp(after.data() + 1048576, tgt.data() + 1048576, SZ - 2 * 1048576) == 0);    // data area untouched
    { FileSource s(tgtPath); auto l = analyze(s); CHECK(l.scheme == Scheme::MBR && l.partitions.size() == 1); }

    // Failure handling
    {
        writeFile(tgtPath, tgt); FileSource rb(tgtPath); FailSink fs;
        auto r = applyRestore(bak, plan, plan.token, fs, rb); CHECK(!r.ok && r.written);
        LyingSink ls; r = applyRestore(bak, plan, plan.token, ls, rb);
        CHECK(!r.ok && r.error.find("verification") != std::string::npos);
    }
    CHECK(readFile(tgtPath) == tgt);
    // FileSink never grows a file nor creates one
    { FileSink sk(tgtPath); uint8_t b = 1; CHECK(!sk.write(SZ, &b, 1) && !sk.write(SZ - 1, "ab", 2)); }
    { FileSink sk(tmpPath("dp_does_not_exist.img")); CHECK(!sk.ok()); CHECK(!std::filesystem::exists(tmpPath("dp_does_not_exist.img"))); }
    // Corrupt backup is rejected
    { auto v = readFile(bakPath); v[v.size() - 3] ^= 0xFF; writeFile(bakPath, v); CHECK(!loadPartitionBackup(bakPath).ok); }

    for (auto* p : {srcPath.c_str(), bakPath.c_str(), tgtPath.c_str()}) std::filesystem::remove(p);
    std::printf(failures ? "restore: %d failures\n" : "restore: all passed\n", failures);
    return failures ? 1 : 0;
}


