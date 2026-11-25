// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
// diskpro-tools: read-only utilities. Source is a file or "disk:N" (physical drive, needs admin).
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include "tools.h"
#ifdef _WIN32
#include "device.h"
#endif

using namespace diskpro;

static std::unique_ptr<ByteSource> open(const std::string& s) {
#ifdef _WIN32
    if (s.rfind("disk:", 0) == 0) {
        char* e = nullptr;
        long n = std::strtol(s.c_str() + 5, &e, 10);
        if (*e || n < 0) return nullptr;
        auto d = std::make_unique<DeviceSource>(int(n));
        if (!d->ok()) return nullptr;
        return d;
    }
#endif
    auto f = std::make_unique<FileSource>(s);
    if (!f->ok()) return nullptr;
    return f;
}

static void prog(const Progress& p) {
    if (p.total) std::fprintf(stderr, "\r%5.1f%%  bad blocks: %llu ", 100.0 * double(p.done) / double(p.total), (unsigned long long)p.badBlocks);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage:\n  diskpro-tools hash <src>\n  diskpro-tools image <src> <out.img>   (never overwrites)\n"
                     "  diskpro-tools scan <src>\n  diskpro-tools ptbackup <src> <out.ptb>   (saves first/last MiB; never overwrites)\n  diskpro-tools ptverify <file.ptb>\n  diskpro-tools hex <src> <offset> [len]\n"
                     "src = file or disk:N (read-only; admin needed for disks)\n");
        return 2;
    }
    std::string cmd = argv[1];
    if (cmd == "ptverify") {
        auto r = verifyPartitionBackup(argv[2]);
        if (!r.ok) { std::fprintf(stderr, "INVALID: %s\n", r.error.c_str()); return 1; }
        std::printf("OK scheme=%s diskSize=%llu head=%llu tail=%llu sha256=%s\n", r.scheme.c_str(), (unsigned long long)r.diskSize, (unsigned long long)r.headBytes, (unsigned long long)r.tailBytes, r.sha256.c_str());
        return 0;
    }
    auto src = open(argv[2]);
    if (!src) { std::fprintf(stderr, "cannot open source (not found or access denied)\n"); return 1; }
    std::atomic<bool> cancel{false};
    if (cmd == "hash" || cmd == "image") {
        ImageResult r;
        if (cmd == "image") {
            if (argc < 4) return 2;
            r = createImage(*src, argv[3], cancel, prog);
        } else {
            r = hashSource(*src, cancel, prog);
        }
        std::fprintf(stderr, "\n");
        if (!r.ok) { std::fprintf(stderr, "error: %s\n", r.error.c_str()); return 1; }
        std::printf("bytes=%llu badBlocks=%llu sha256=%s\n", (unsigned long long)r.bytes, (unsigned long long)r.badBlocks, r.sha256.c_str());
        return r.badBlocks ? 3 : 0;
    }
    if (cmd == "ptbackup") {
        if (argc < 4) return 2;
        auto r = backupPartitionTable(*src, argv[3]);
        if (!r.ok) { std::fprintf(stderr, "error: %s\n", r.error.c_str()); return 1; }
        std::printf("saved scheme=%s head=%llu tail=%llu sha256=%s\n", r.scheme.c_str(), (unsigned long long)r.headBytes, (unsigned long long)r.tailBytes, r.sha256.c_str());
        return 0;
    }
    if (cmd == "scan") {
        auto r = surfaceScan(*src, cancel, prog);
        std::fprintf(stderr, "\n");
        std::printf("blocks=%llu bad=%llu speed=%.1f MB/s\n", (unsigned long long)r.blocks, (unsigned long long)r.badBlocks, r.mbPerSec);
        for (auto o : r.badOffsets) std::printf("bad offset %llu\n", (unsigned long long)o);
        return r.badBlocks ? 3 : 0;
    }
    if (cmd == "hex") {
        if (argc < 4) return 2;
        uint64_t off = std::strtoull(argv[3], nullptr, 0);
        size_t len = argc > 4 ? size_t(std::strtoull(argv[4], nullptr, 0)) : 512;
        auto s = hexDump(*src, off, len);
        if (s.empty()) { std::fprintf(stderr, "read error or offset out of range\n"); return 1; }
        std::fputs(s.c_str(), stdout);
        return 0;
    }
    return 2;
}

