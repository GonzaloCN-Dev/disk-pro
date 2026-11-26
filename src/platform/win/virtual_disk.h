// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
// Controlled VHD/VHDX lifecycle through the Windows Virtual Disk API.
#pragma once
#include <cstdint>
#include <string>

namespace diskpro::win {

struct VirtualDiskResult {
    bool ok = false;
    std::string error;
};

// Creates a new fixed or dynamically expanding VHD/VHDX. Existing files are never overwritten.
VirtualDiskResult createVirtualDisk(const std::string& path, uint64_t sizeBytes, bool dynamic);

// Attaches an existing VHD/VHDX. Read-only is the default; no drive letter is assigned automatically.
VirtualDiskResult attachVirtualDisk(const std::string& path, bool readOnly);
VirtualDiskResult detachVirtualDisk(const std::string& path);

}  // namespace diskpro::win
