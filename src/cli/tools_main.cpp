// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
// diskpro-tools: read-only utilities. Source is a file or "disk:N" (physical drive, needs admin).
#include <atomic>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include "recover.h"
#ifdef _WIN32
#include "device.h"
#endif

using namespace diskpro;

static std::shared_ptr<ByteSource> open(const std::string& s) {
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

static std::shared_ptr<RangeSource> partition(const std::shared_ptr<ByteSource>& src, int index, uint32_t ss,
                                              std::string& error) {
    auto layout = analyze(*src, ss);
    if (layout.hasErrors() || (layout.scheme != Scheme::MBR && layout.scheme != Scheme::GPT)) {
        error = "source has no valid partition table"; return {};
    }
    if (index < 1 || index > int(layout.partitions.size())) { error = "partition index does not exist"; return {}; }
    const auto& p = layout.partitions[size_t(index - 1)];
    return std::make_shared<RangeSource>(src, p.firstLba * ss, p.sectorCount * ss);
}

static void prog(const Progress& p) {
    if (p.total) std::fprintf(stderr, "\r%5.1f%%  bad blocks: %llu ", 100.0 * double(p.done) / double(p.total), (unsigned long long)p.badBlocks);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage:\n  diskpro-tools hash <src>\n  diskpro-tools verify <src> <sha256>\n  diskpro-tools image <src> <out.img>   (never overwrites)\n"
                     "  diskpro-tools scan <src>\n  diskpro-tools ptbackup <src> <out.ptb>   (saves first/last MiB; never overwrites)\n  diskpro-tools ptverify <file.ptb>\n  diskpro-tools hex <src> <offset> [len]\n  diskpro-tools recover <src> [512|4096]   (finds lost NTFS/FAT32/exFAT partitions; read-only)\n"
                     "  diskpro-tools files <src> <partition> [512|4096]   (lists deleted FAT32 files; read-only)\n"
                     "  diskpro-tools extract <src> <partition> <candidate> <new-file> [512|4096]\n"
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
    if (cmd == "verify" && argc != 4) return 2;
    auto src = open(argv[2]);
    if (!src) { std::fprintf(stderr, "cannot open source (not found or access denied)\n"); return 1; }
    std::atomic<bool> cancel{false};
    if (cmd == "files" || cmd == "extract") {
        if ((cmd == "files" && (argc < 4 || argc > 5)) || (cmd == "extract" && (argc < 6 || argc > 7))) return 2;
        char* end = nullptr;
        long partIndex = std::strtol(argv[3], &end, 10);
        if (*end || partIndex < 1) { std::fprintf(stderr, "invalid partition index\n"); return 2; }
        const int ssArg = cmd == "files" ? 4 : 6;
        uint32_t ss = argc > ssArg ? uint32_t(std::strtoul(argv[ssArg], &end, 10)) : 512;
        if ((ss != 512 && ss != 4096) || (argc > ssArg && *end)) { std::fprintf(stderr, "invalid sector size\n"); return 2; }
        std::string error;
        auto volume = partition(src, int(partIndex), ss, error);
        if (!volume) { std::fprintf(stderr, "error: %s\n", error.c_str()); return 1; }
        auto scan = findDeletedFat32Files(*volume, cancel, prog);
        std::fprintf(stderr, "\n");
        if (!scan.ok) { std::fprintf(stderr, "error: %s\n", scan.error.empty() ? "cancelled" : scan.error.c_str()); return 1; }
        if (cmd == "files") {
            std::printf("found=%zu\n", scan.files.size());
            for (size_t i = 0; i < scan.files.size(); ++i) {
                const auto& f = scan.files[i];
                std::printf("%zu path=%s size=%llu score=%u/100 evidence=%s\n", i + 1, f.path.c_str(),
                            (unsigned long long)f.size, unsigned(f.confidence), f.evidence.c_str());
            }
            return 0;
        }
        long candidate = std::strtol(argv[4], &end, 10);
        if (*end || candidate < 1 || candidate > long(scan.files.size())) { std::fprintf(stderr, "candidate index does not exist\n"); return 2; }
#ifdef _WIN32
        std::string outputGuardError;
        if (volume->backingDiskNumber() >= 0 &&
            !pathOnPhysicalDisk(argv[5], volume->backingDiskNumber(), outputGuardError)) {
            std::fprintf(stderr, "error: %s\n", outputGuardError.c_str());
            return 1;
        }
#endif
        auto recovered = recoverFat32FileToNewFile(*volume, scan.files[size_t(candidate - 1)], argv[5], cancel, prog);
        if (!recovered.ok) { std::fprintf(stderr, "\nerror: %s\n", recovered.error.c_str()); return 1; }
        std::fprintf(stderr, "\n");
        std::printf("recovered=%llu sha256=%s path=%s\n", (unsigned long long)recovered.bytes,
                    recovered.sha256.c_str(), scan.files[size_t(candidate - 1)].path.c_str());
        return 0;
    }
    if (cmd == "hash" || cmd == "image" || cmd == "verify") {
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
        if (cmd == "verify") {
            if (argc != 4) return 2;
            std::string expected = argv[3];
            std::transform(expected.begin(), expected.end(), expected.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (expected.size() != 64 || expected.find_first_not_of("0123456789abcdef") != std::string::npos) {
                std::fprintf(stderr, "invalid SHA-256\n"); return 2;
            }
            if (expected != r.sha256) { std::fprintf(stderr, "MISMATCH\n"); return 4; }
            std::printf("VERIFIED\n");
        }
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
    if (cmd == "recover") {
        uint32_t ss = argc > 3 ? uint32_t(std::strtoul(argv[3], nullptr, 10)) : 512;
        auto r = findLostPartitions(*src, ss, cancel, prog);
        std::fprintf(stderr, "\n");
        if (!r.ok) { std::fprintf(stderr, "error: %s\n", r.error.empty() ? "cancelled" : r.error.c_str()); return 1; }
        std::printf("scanned=%llu found=%zu\n", (unsigned long long)r.scannedBytes, r.found.size());
        for (auto& f : r.found)
            std::printf("%-6s firstLba=%llu sectors=%llu size=%s score=%u/100 evidence=%s%s\n", f.filesystem.c_str(), (unsigned long long)f.firstLba,
                        (unsigned long long)f.sectorCount, humanSize(f.sectorCount * ss).c_str(), unsigned(f.confidence), f.evidence.c_str(),
                        f.overlapsExisting ? "  (already in the table)" : "  (LOST)");
        return 0;
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
