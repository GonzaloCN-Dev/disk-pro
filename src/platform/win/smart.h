// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
// Read-only health queries. Uses only query IOCTLs opened with access 0 (no elevation, no writes).
#pragma once
#include <cstdint>
#include <string>

namespace diskpro::win {

struct HealthInfo {
    bool nvmeAvailable = false;       // NVMe SMART/Health log page read
    uint8_t criticalWarning = 0;      // bitmask, 0 = none
    int temperatureC = -1;
    uint8_t availableSparePct = 0;
    uint8_t percentageUsed = 0;
    uint64_t powerOnHours = 0;
    uint64_t unsafeShutdowns = 0;
    uint64_t mediaErrors = 0;
    bool predictFailureAvailable = false;  // generic IOCTL_STORAGE_PREDICT_FAILURE (ATA SMART status)
    bool predictFailure = false;
    std::string note;                      // why data is unavailable
};

HealthInfo queryHealth(uint32_t diskNumber);

}  // namespace diskpro::win
