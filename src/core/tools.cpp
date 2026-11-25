// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
#include "tools.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace diskpro {

namespace {
const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98,
    0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8,
    0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
    0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2};
inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
}  // namespace

Sha256::Sha256() {
    const uint32_t i[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::memcpy(h_, i, sizeof h_);
}

void Sha256::block(const uint8_t* p) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = uint32_t(p[i * 4]) << 24 | uint32_t(p[i * 4 + 1]) << 16 | uint32_t(p[i * 4 + 2]) << 8 | p[i * 4 + 3];
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d; h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += h;
}

void Sha256::update(const void* data, size_t len) {
    auto* p = static_cast<const uint8_t*>(data);
    total_ += len;
    while (len) {
        size_t k = std::min(len, sizeof buf_ - n_);
        std::memcpy(buf_ + n_, p, k);
        n_ += k; p += k; len -= k;
        if (n_ == 64) { block(buf_); n_ = 0; }
    }
}

std::string Sha256::finalHex() {
    uint64_t bits = total_ * 8;
    uint8_t pad = 0x80;
    update(&pad, 1);
    uint8_t z = 0;
    while (n_ != 56) update(&z, 1);
    uint8_t lenb[8];
    for (int i = 0; i < 8; ++i) lenb[i] = uint8_t(bits >> (56 - 8 * i));
    update(lenb, 8);
    char out[65];
    for (int i = 0; i < 8; ++i) std::snprintf(out + i * 8, 9, "%08x", h_[i]);
    return out;
}

namespace {
// Reads one block; on failure retries 512-byte pieces so only truly bad sectors are zeroed.
uint64_t readBlock(const ByteSource& s, uint64_t off, std::vector<uint8_t>& buf, size_t len, bool& bad) {
    bad = false;
    if (s.read(off, buf.data(), len)) return 0;
    bad = true;
    uint64_t badBytes = 0;
    for (size_t o = 0; o < len; o += 512) {
        size_t n = std::min<size_t>(512, len - o);
        if (!s.read(off + o, buf.data() + o, n)) { std::memset(buf.data() + o, 0, n); badBytes += n; }
    }
    return badBytes;
}
}  // namespace

static ImageResult pump(const ByteSource& src, std::FILE* out, const std::atomic<bool>& cancel,
                        const ProgressFn& progress, size_t block) {
    ImageResult r;
    if (block == 0) block = 1 << 20;
    uint64_t total = src.size();
    std::vector<uint8_t> buf(block);
    Sha256 h;
    Progress pr; pr.total = total;
    for (uint64_t off = 0; off < total; off += block) {
        if (cancel.load()) { r.cancelled = true; r.error = "cancelled"; return r; }
        size_t len = size_t(std::min<uint64_t>(block, total - off));
        bool bad;
        readBlock(src, off, buf, len, bad);
        if (bad) ++r.badBlocks;
        h.update(buf.data(), len);
        if (out && std::fwrite(buf.data(), 1, len, out) != len) { r.error = "write error (disk full?)"; return r; }
        r.bytes += len;
        pr.done = r.bytes; pr.badBlocks = r.badBlocks;
        if (progress) progress(pr);
    }
    r.sha256 = h.finalHex();
    r.ok = true;
    return r;
}

ImageResult createImage(const ByteSource& src, const std::string& outPath, const std::atomic<bool>& cancel,
                        const ProgressFn& progress, size_t block) {
    namespace fs = std::filesystem;
    std::error_code ec;
    ImageResult r;
    if (fs::exists(fs::u8path(outPath), ec)) { r.error = "output file already exists"; return r; }
    std::FILE* f = nullptr;
#ifdef _WIN32
    f = _wfopen(fs::u8path(outPath).c_str(), L"wbx");
#else
    f = std::fopen(outPath.c_str(), "wbx");
#endif
    if (!f) { r.error = "cannot create output file"; return r; }
    r = pump(src, f, cancel, progress, block);
    if (std::fclose(f) != 0 && r.ok) { r.ok = false; r.error = "close error"; }
    if (!r.ok) fs::remove(fs::u8path(outPath), ec);
    return r;
}

ImageResult hashSource(const ByteSource& src, const std::atomic<bool>& cancel, const ProgressFn& progress, size_t block) {
    return pump(src, nullptr, cancel, progress, block);
}

ScanResult surfaceScan(const ByteSource& src, const std::atomic<bool>& cancel, const ProgressFn& progress, size_t block) {
    ScanResult r;
    if (block == 0) block = 1 << 20;
    uint64_t total = src.size();
    std::vector<uint8_t> buf(block);
    auto t0 = std::chrono::steady_clock::now();
    Progress pr; pr.total = total;
    uint64_t done = 0;
    for (uint64_t off = 0; off < total; off += block) {
        if (cancel.load()) { r.cancelled = true; break; }
        size_t len = size_t(std::min<uint64_t>(block, total - off));
        ++r.blocks;
        if (!src.read(off, buf.data(), len)) {
            ++r.badBlocks;
            if (r.badOffsets.size() < 1000) r.badOffsets.push_back(off);
        }
        done += len;
        pr.done = done; pr.badBlocks = r.badBlocks;
        if (progress) progress(pr);
    }
    double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    r.mbPerSec = s > 0 ? double(done) / 1048576.0 / s : 0;
    r.ok = !r.cancelled;
    return r;
}

std::string hexDump(const ByteSource& src, uint64_t offset, size_t len) {
    if (len > (1u << 20)) len = 1u << 20;
    if (offset >= src.size()) return {};
    len = size_t(std::min<uint64_t>(len, src.size() - offset));
    std::vector<uint8_t> b(len);
    if (!src.read(offset, b.data(), len)) return {};
    std::string out;
    char line[128];
    for (size_t i = 0; i < len; i += 16) {
        int n = std::snprintf(line, sizeof line, "%08llx  ", (unsigned long long)(offset + i));
        for (size_t j = 0; j < 16; ++j) {
            if (i + j < len) n += std::snprintf(line + n, sizeof line - n, "%02x ", b[i + j]);
            else n += std::snprintf(line + n, sizeof line - n, "   ");
            if (j == 7) line[n++] = ' ';
        }
        n += std::snprintf(line + n, sizeof line - n, " |");
        for (size_t j = 0; j < 16 && i + j < len; ++j) line[n++] = (b[i + j] >= 32 && b[i + j] < 127) ? char(b[i + j]) : '.';
        line[n++] = '|'; line[n++] = '\n'; line[n] = 0;
        out += line;
    }
    return out;
}

}  // namespace diskpro
