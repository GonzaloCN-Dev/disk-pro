// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
// Read-only health queries. Uses only query IOCTLs opened with access 0 (no elevation, no writes).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace diskpro::win {

enum class HealthStatus : uint8_t { Available, Unsupported, QueryError, OpenError };

struct HealthInfo {
    HealthStatus status = HealthStatus::Unsupported;
    uint32_t errorCode = 0;
    bool nvmeAvailable = false;       // NVMe SMART/Health log page read
    uint8_t criticalWarning = 0;      // bitmask, 0 = none
    int temperatureC = -1;
    uint8_t availableSparePct = 0;
    uint8_t percentageUsed = 0;
    uint64_t powerOnHours = 0;
    uint64_t unsafeShutdowns = 0;
    uint64_t mediaErrors = 0;
    bool ataAvailable = false;
    uint64_t reallocatedSectors = 0;
    uint64_t pendingSectors = 0;
    uint64_t uncorrectableSectors = 0;
    bool predictFailureAvailable = false;  // generic IOCTL_STORAGE_PREDICT_FAILURE (ATA SMART status)
    bool predictFailure = false;
    std::string note;                      // why data is unavailable
};

HealthInfo queryHealth(uint32_t diskNumber);
// Pure classification helper shared with tests. A failed IOCTL is unsupported only for
// explicitly documented "not supported/invalid function" errors; other failures are errors.
HealthStatus classifyHealthStatus(bool opened, bool dataAvailable, bool allQueriesUnsupported);
// Parses the standard 512-byte ATA SMART READ DATA payload returned in STORAGE_PREDICT_FAILURE.
// Exposed for deterministic tests; unknown vendor attributes are ignored.
bool parseAtaSmart(const uint8_t* data, size_t size, HealthInfo& health);

}  // namespace diskpro::win
