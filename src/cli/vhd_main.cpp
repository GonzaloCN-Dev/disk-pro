// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "virtual_disk.h"

using diskpro::win::VirtualDiskResult;

namespace {
void usage() {
    std::fprintf(stderr,
        "usage:\n"
        "  diskpro-vhd create FILE.vhd|FILE.vhdx --size-mib N [--dynamic]\n"
        "  diskpro-vhd attach FILE.vhd|FILE.vhdx [--read-write]\n"
        "  diskpro-vhd detach FILE.vhd|FILE.vhdx\n"
        "Attach is read-only by default and does not assign a drive letter.\n");
}
}

int main(int argc, char** argv) {
    if (argc < 3) { usage(); return 2; }
    const std::string op = argv[1], path = argv[2];
    VirtualDiskResult r;
    if (op == "create") {
        uint64_t mib = 0;
        bool dynamic = false;
        for (int i = 3; i < argc; ++i) {
            if (!std::strcmp(argv[i], "--dynamic")) dynamic = true;
            else if (!std::strcmp(argv[i], "--size-mib") && i + 1 < argc) {
                char* end = nullptr;
                mib = std::strtoull(argv[++i], &end, 10);
                if (!end || *end) { std::fprintf(stderr, "invalid --size-mib\n"); return 2; }
            } else { usage(); return 2; }
        }
        if (!mib || mib > (2ull << 20)) { std::fprintf(stderr, "--size-mib is required and must not exceed 2097152\n"); return 2; }
        r = diskpro::win::createVirtualDisk(path, mib << 20, dynamic);
    } else if (op == "attach") {
        bool readOnly = true;
        for (int i = 3; i < argc; ++i) {
            if (!std::strcmp(argv[i], "--read-write")) readOnly = false;
            else { usage(); return 2; }
        }
        r = diskpro::win::attachVirtualDisk(path, readOnly);
    } else if (op == "detach") {
        if (argc != 3) { usage(); return 2; }
        r = diskpro::win::detachVirtualDisk(path);
    } else {
        usage(); return 2;
    }
    if (!r.ok) { std::fprintf(stderr, "FAILED: %s\n", r.error.c_str()); return 1; }
    std::printf("OK\n");
    return 0;
}
