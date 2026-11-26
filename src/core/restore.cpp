// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
#include "restore.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>
#ifdef _WIN32
#include <share.h>
#endif
#include "tools.h"

namespace diskpro {

FileSink::FileSink(const std::string& path) {
    std::error_code ec;
    auto p = std::filesystem::u8path(path);
    if (!std::filesystem::is_regular_file(p, ec)) return;
    size_ = std::filesystem::file_size(p, ec);
#ifdef _WIN32
    f_ = _wfsopen(p.c_str(), L"r+b", _SH_DENYNO);
#else
    f_ = std::fopen(path.c_str(), "r+b");
#endif
}
FileSink::~FileSink() { if (f_) std::fclose(static_cast<std::FILE*>(f_)); }

bool FileSink::write(uint64_t off, const void* buf, size_t len) {
    if (!f_ || off > size_ || len > size_ - off) return false;  // never grows the file
    auto* f = static_cast<std::FILE*>(f_);
#ifdef _WIN32
    if (_fseeki64(f, long long(off), SEEK_SET) != 0) return false;
#else
    if (fseeko(f, off_t(off), SEEK_SET) != 0) return false;
#endif
    return std::fwrite(buf, 1, len, f) == len;
}
bool FileSink::flush() { return f_ && std::fflush(static_cast<std::FILE*>(f_)) == 0; }

namespace {
const char kMagic[] = "DISKPRO-PTBACKUP 1\n";
}

BackupData loadPartitionBackup(const std::string& path) {
    BackupData d;
    std::FILE* f =
#ifdef _WIN32
        _wfopen(std::filesystem::u8path(path).c_str(), L"rb");
#else
        std::fopen(path.c_str(), "rb");
#endif
    if (!f) { d.error = "cannot open file"; return d; }
    std::string all;
    std::vector<char> buf(65536);
    size_t n;
    while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0) { all.append(buf.data(), n); if (all.size() > (8u << 20)) break; }
    std::fclose(f);
    size_t end = all.find("\n\n");
    if (all.compare(0, sizeof kMagic - 1, kMagic) != 0 || end == std::string::npos) { d.error = "not a Disk Pro backup"; return d; }
    std::string hdr = all.substr(0, end + 2);
    auto get = [&](const char* k) {
        size_t p = hdr.find(std::string("\n") + k + "=");
        if (p == std::string::npos) return std::string();
        p += std::strlen(k) + 2;
        return hdr.substr(p, hdr.find('\n', p) - p);
    };
    d.scheme = get("scheme");
    d.diskId = get("diskId");
    d.sectorSize = uint32_t(std::strtoul(get("sectorSize").c_str(), nullptr, 10));
    d.diskSize = std::strtoull(get("diskSize").c_str(), nullptr, 10);
    uint64_t hb = std::strtoull(get("headBytes").c_str(), nullptr, 10), tb = std::strtoull(get("tailBytes").c_str(), nullptr, 10);
    const uint64_t kArea = 1u << 20;
    if (hb > kArea || tb > kArea || hb == 0 || all.size() != hdr.size() + hb + tb) { d.error = "size mismatch (truncated or corrupt)"; return d; }
    if (d.sectorSize != 512 && d.sectorSize != 4096) { d.error = "invalid sector size"; return d; }
    if (hb + tb > d.diskSize) { d.error = "invalid disk size"; return d; }
    Sha256 h;
    h.update(all.data() + hdr.size(), size_t(hb + tb));
    d.sha256 = h.finalHex();
    if (d.sha256 != get("sha256")) { d.error = "SHA-256 mismatch (corrupt)"; return d; }
    auto* base = reinterpret_cast<const uint8_t*>(all.data()) + hdr.size();
    d.head.assign(base, base + hb);
    d.tail.assign(base + hb, base + hb + tb);
    d.ok = true;
    return d;
}

RestorePlan planRestore(const BackupData& b, const TargetFacts& t) {
    RestorePlan p;
    p.targetSize = t.size;
    p.sectorSize = t.sectorSize;
    p.token = "RESTORE DISK " + std::to_string(t.number);
    if (!b.ok) p.blockers.push_back("The backup file is not valid: " + b.error);
    if (t.isSystemDisk) p.blockers.push_back("Target is the SYSTEM disk: writing is forbidden.");
    if (!t.isVirtual) p.blockers.push_back("Target is not a virtual disk: this version only writes to virtual disks (VHD/VHDX).");
    if (t.size > kMaxWritableDisk) p.blockers.push_back("Target is larger than 4 GiB: this version refuses to write to it.");
    if (t.size == 0) p.blockers.push_back("Target size is unknown.");
    if (b.ok) {
        if (b.diskSize != t.size) p.blockers.push_back("Target size differs from the size recorded in the backup.");
        if (b.sectorSize != t.sectorSize) p.blockers.push_back("Target sector size differs from the backup.");
        uint64_t ss = t.sectorSize ? t.sectorSize : 512;
        auto add = [&](uint64_t off, uint64_t len) {
            if (len == 0) return;
            if (off % ss || len % ss) { p.blockers.push_back("A range is not sector-aligned."); return; }
            if (off > t.size || len > t.size - off) { p.blockers.push_back("A range is outside the disk."); return; }
            p.ranges.push_back({off, len});
        };
        add(0, b.head.size());
        if (b.tail.size() <= t.size) add(t.size - b.tail.size(), b.tail.size());
        if (!b.tail.empty() && b.head.size() + b.tail.size() > t.size) p.blockers.push_back("Ranges overlap.");
        if (!t.currentDiskId.empty() && t.currentDiskId != b.diskId)
            p.warnings.push_back("The backup comes from a different disk (identifier differs): the target will take that identity.");
        if (t.currentScheme != "RAW" && !t.currentScheme.empty())
            p.warnings.push_back("The current " + t.currentScheme + " partition table will be REPLACED. Partitions not described in the backup become inaccessible.");
        p.warnings.push_back("Only the partition-table areas are written; partition contents are never touched or restored.");
    }
    if (!p.blockers.empty()) p.ranges.clear();
    p.allowed = p.blockers.empty() && !p.ranges.empty();
    return p;
}

RestoreResult applyRestore(const BackupData& b, const RestorePlan& plan, const std::string& typed, ByteSink& sink,
                           const ByteSource& readback) {
    RestoreResult r;
    if (!plan.allowed || !b.ok) { r.error = "operation not allowed by the plan"; return r; }
    if (typed != plan.token) { r.error = "confirmation text does not match"; return r; }
    const uint64_t size = readback.size();
    if (size != plan.targetSize || size != b.diskSize || plan.sectorSize != b.sectorSize) {
        r.error = "target size or sector size changed since preview"; return r;
    }
    struct Item { uint64_t off; const std::vector<uint8_t>* data; };
    std::vector<Item> items;
    if (!b.head.empty()) items.push_back({0, &b.head});
    if (!b.tail.empty()) items.push_back({size - b.tail.size(), &b.tail});
    for (auto& it : items) {
        if (it.data->size() > size || it.off > size - it.data->size()) { r.error = "range outside the disk"; return r; }
        bool inPlan = false;
        for (auto& w : plan.ranges) inPlan |= (w.offset == it.off && w.length == it.data->size());
        if (!inPlan) { r.error = "range not in plan"; return r; }
    }
    if (!sink.prepare(0, size)) { r.error = "one or more target volumes could not be locked and dismounted"; return r; }
    for (auto& it : items) {
        r.written = true;
        if (!sink.write(it.off, it.data->data(), it.data->size())) { r.error = "write failed"; return r; }
    }
    if (!sink.flush()) { r.error = "flush failed"; return r; }
    for (auto& it : items) {
        std::vector<uint8_t> back(it.data->size());
        if (!readback.read(it.off, back.data(), back.size()) || std::memcmp(back.data(), it.data->data(), back.size()) != 0) {
            r.error = "verification failed: data read back differs from the backup";
            return r;
        }
    }
    r.ok = true;
    return r;
}

}  // namespace diskpro



