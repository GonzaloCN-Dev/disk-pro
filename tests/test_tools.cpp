#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include "tools.h"

using namespace diskpro;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

// Source with injected bad regions.
class BadSource final : public ByteSource {
public:
    BadSource(std::vector<uint8_t> d, uint64_t badFrom, uint64_t badTo) : d_(std::move(d)), f_(badFrom), t_(badTo) {}
    uint64_t size() const override { return d_.size(); }
    bool read(uint64_t o, void* b, size_t n) const override {
        if (o + n > d_.size()) return false;
        if (o < t_ && o + n > f_) return false;
        std::memcpy(b, d_.data() + o, n);
        return true;
    }
private:
    std::vector<uint8_t> d_; uint64_t f_, t_;
};

static std::string sha(const std::string& s) { Sha256 h; h.update(s.data(), s.size()); return h.finalHex(); }

int main() {
    CHECK(sha("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    {   // million 'a'
        Sha256 h; std::string a(1000, 'a');
        for (int i = 0; i < 1000; ++i) h.update(a.data(), a.size());
        CHECK(h.finalHex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    }

    std::vector<uint8_t> data(3 * 1024 * 1024 + 777);
    for (size_t i = 0; i < data.size(); ++i) data[i] = uint8_t(i * 31 + (i >> 8));
    std::atomic<bool> cancel{false};
    auto tmp = std::filesystem::temp_directory_path();
    auto out = (tmp / "diskpro_test.img").string();
    std::filesystem::remove(out);

    MemorySource ms(data);
    auto r = createImage(ms, out, cancel, {}, 1 << 20);
    CHECK(r.ok && r.bytes == data.size() && r.badBlocks == 0);
    CHECK(std::filesystem::file_size(out) == data.size());
    Sha256 h; h.update(data.data(), data.size());
    CHECK(r.sha256 == h.finalHex());
    {   // file content identical, and hash of the file matches
        FileSource fs(out);
        auto r2 = hashSource(fs, cancel);
        CHECK(r2.ok && r2.sha256 == r.sha256);
    }
    auto again = createImage(ms, out, cancel);   // must not overwrite
    CHECK(!again.ok);
    std::filesystem::remove(out);

    // bad region: one 1 MiB block unreadable -> flagged, zeros in image, other data intact
    BadSource bs(data, 1048576 + 1024, 1048576 + 2048);
    auto rb = createImage(bs, out, cancel);
    CHECK(rb.ok && rb.badBlocks == 1);
    {
        std::ifstream f(out, std::ios::binary);
        std::vector<char> c(data.size());
        f.read(c.data(), std::streamsize(c.size()));
        CHECK(std::memcmp(c.data(), data.data(), 1048576 + 1024) == 0);
        bool zeros = true;
        for (size_t i = 1048576 + 1024; i < 1048576 + 2048; ++i) zeros &= c[i] == 0;
        CHECK(zeros);
        CHECK(std::memcmp(c.data() + 1048576 + 2048, data.data() + 1048576 + 2048, data.size() - 1048576 - 2048) == 0);
    }
    std::filesystem::remove(out);

    auto sc = surfaceScan(bs, cancel);
    CHECK(sc.ok && sc.blocks == 4 && sc.badBlocks == 1 && sc.badOffsets.size() == 1 && sc.badOffsets[0] == 1048576);

    // cancellation leaves no file
    std::atomic<bool> c2{true};
    auto rc = createImage(ms, out, c2);
    CHECK(!rc.ok && rc.cancelled && !std::filesystem::exists(out));

    auto hd = hexDump(ms, 0, 32);
    CHECK(hd.find("00000000  00 1f 3e") == 0);
    CHECK(hexDump(ms, data.size(), 16).empty());

    {   // partition table backup
        auto pb = (tmp / "diskpro_test.ptb").string(); std::filesystem::remove(pb);
        auto br = backupPartitionTable(ms, pb);
        CHECK(br.ok && br.headBytes == 1048576 && br.tailBytes == 1048576);
        auto vr = verifyPartitionBackup(pb);
        CHECK(vr.ok && vr.sha256 == br.sha256);
        CHECK(!backupPartitionTable(ms, pb).ok);
        { std::fstream x(pb, std::ios::in | std::ios::out | std::ios::binary); x.seekp(-10, std::ios::end); x.put(char(0x55)); x.put(char(0x66)); }
        CHECK(!verifyPartitionBackup(pb).ok);
        std::filesystem::resize_file(pb, std::filesystem::file_size(pb) - 5);
        CHECK(!verifyPartitionBackup(pb).ok);
        CHECK(!verifyPartitionBackup((tmp / "nonexistent.ptb").string()).ok);
        BadSource bad(data, 0, 512);
        CHECK(!backupPartitionTable(bad, (tmp / "x.ptb").string()).ok && !std::filesystem::exists(tmp / "x.ptb"));
        std::filesystem::remove(pb);
    }

    std::printf(failures ? "tools: %d failures\n" : "tools: all passed\n", failures);
    return failures ? 1 : 0;
}

