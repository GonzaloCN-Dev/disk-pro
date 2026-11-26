// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
#include "clone.h"
#include <cstring>

namespace diskpro {

ClonePlan planClone(const CloneSourceFacts& s, const TargetFacts& t) {
    ClonePlan p;
    p.targetSize = t.size;
    p.sectorSize = t.sectorSize;
    p.token = "CLONE TO DISK " + std::to_string(t.number);
    if (s.isTargetItself) p.blockers.push_back("Source and target are the same disk.");
    if (t.isSystemDisk) p.blockers.push_back("Target is the SYSTEM disk: writing is forbidden.");
    if (!t.isVirtual) p.blockers.push_back("Target is not a virtual disk: this version only writes to virtual disks (VHD/VHDX).");
    if (t.size > kMaxWritableDisk) p.blockers.push_back("Target is larger than 4 GiB: this version refuses to write to it.");
    if (t.size == 0) p.blockers.push_back("Target size is unknown.");
    if (s.size == 0) p.blockers.push_back("Source size is unknown or empty.");
    if (s.size > t.size) p.blockers.push_back("Source is larger than the target: it would not fit.");
    uint64_t ss = t.sectorSize ? t.sectorSize : 512;
    if (s.sectorSize != t.sectorSize) p.blockers.push_back("Source and target sector sizes differ.");
    if (s.size % ss) p.blockers.push_back("Source size is not a multiple of the sector size.");
    if (s.size && s.size < t.size)
        p.warnings.push_back("Target is larger than the source: the extra space stays as it was and, for GPT, the backup header will not be at the end of the disk.");
    p.warnings.push_back("EVERYTHING on the target disk will be overwritten, including its partition table and data.");
    p.bytes = s.size;
    p.allowed = p.blockers.empty();
    if (!p.allowed) p.bytes = 0;
    return p;
}

CloneResult applyClone(const ByteSource& src, ByteSink& dst, const ByteSource& back, const ClonePlan& plan,
                       const std::string& typed, const CloneProgress& progress, bool verify) {
    CloneResult r;
    if (!plan.allowed || plan.bytes == 0) { r.error = "operation not allowed by the plan"; return r; }
    if (typed != plan.token) { r.error = "confirmation text does not match"; return r; }
    if (back.size() != plan.targetSize) { r.error = "target size changed since preview"; return r; }
    if (plan.bytes > src.size() || plan.bytes > back.size()) { r.error = "plan range exceeds the disks"; return r; }
    if (!dst.prepare(0, back.size())) { r.error = "one or more target volumes could not be locked and dismounted"; return r; }
    const uint64_t kBlock = 1u << 20, total = plan.bytes;
    std::vector<uint8_t> a(kBlock), b(kBlock);
    for (uint64_t off = 0; off < total;) {
        size_t n = size_t(std::min<uint64_t>(kBlock, total - off));
        if (progress && !progress(0, off, total)) { r.cancelled = true; r.error = "cancelled"; return r; }
        if (!src.read(off, a.data(), n)) { r.error = "read error on the source"; return r; }
        r.written = true;
        if (!dst.write(off, a.data(), n)) { r.error = "write failed"; return r; }
        off += n;
        r.copied = off;
    }
    if (progress) progress(0, total, total);
    if (!dst.flush()) { r.error = "flush failed"; return r; }
    if (verify) {
        for (uint64_t off = 0; off < total;) {
            size_t n = size_t(std::min<uint64_t>(kBlock, total - off));
            if (progress && !progress(1, off, total)) { r.cancelled = true; r.error = "verification cancelled (the copy is complete but unverified)"; return r; }
            if (!src.read(off, a.data(), n) || !back.read(off, b.data(), n)) { r.error = "read error during verification"; return r; }
            if (std::memcmp(a.data(), b.data(), n) != 0) { r.error = "verification failed: target differs from source at offset " + std::to_string(off); return r; }
            off += n;
        }
        if (progress) progress(1, total, total);
    }
    r.ok = true;
    return r;
}

}  // namespace diskpro
