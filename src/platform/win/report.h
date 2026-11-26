// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
// Plain-text report of the disk inventory. Pure formatting: no I/O, no device access.
#pragma once
#include <string>
#include <vector>
#include "inventory.h"

namespace diskpro::win {

// health[i] is the one-line health text of disks[i] (may be shorter than disks).
std::string buildReport(const std::vector<DiskInfo>& disks, const std::vector<std::string>& health, const std::string& appVersion);

}  // namespace diskpro::win
