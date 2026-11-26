// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
// Update logic without I/O: release parsing and version comparison. Network code lives in src/platform/win/updater.*.
#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace diskpro::update {

struct Release {
    bool ok = false;
    std::string error;
    std::string tag, version, notes;
    std::string installerUrl, installerName;
    std::string sha256;  // lowercase hex; a release without a verifiable SHA-256 is refused
    int64_t size = 0;
};

constexpr const char* kAssetPrefix = "https://github.com/GonzaloCN-Dev/disk-pro/releases/download/";

// Builds a Release from the GitHub "latest release" JSON. Only assets served from kAssetPrefix are considered.
// fetchText downloads a small text asset (SHA256SUMS.txt) when the installer asset carries no digest.
Release parseRelease(const std::string& body, const std::function<std::string(const std::string&)>& fetchText);

// True when remote is a higher dotted version than local ("0.10.0" > "0.9.2"). Suffixes like "-beta" are ignored.
bool isNewer(const std::string& remote, const std::string& local);

// Finds "<hash>  <name>" in a SHA256SUMS file. Empty if absent or malformed.
std::string findHashInSums(const std::string& sums, const std::string& fileName);

}  // namespace diskpro::update
