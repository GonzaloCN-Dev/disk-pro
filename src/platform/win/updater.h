// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
// Opt-in self-update over HTTPS (WinHTTP). Only a plain GET to github.com is ever sent: no identifiers, no telemetry.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include "update.h"

namespace diskpro::win {

// Queries the latest release. Blocking: call from a worker thread.
update::Release fetchLatestRelease();

// Downloads an official release asset to path (through a .part file). Returns "" on success, otherwise an error.
std::string downloadAsset(const std::string& url, const std::string& path, const std::function<void(int64_t, int64_t)>& progress,
                          const std::function<bool()>& cancelled);

// True if the file's SHA-256 equals expectedHex. On mismatch or read error the file is deleted.
bool verifyDownloaded(const std::string& path, const std::string& expectedHex);

// %TEMP%\DiskPro-update
std::string updateDir();

// Starts the installer silently and returns. The caller must exit so Setup can replace the files.
bool runInstaller(const std::string& path);

}  // namespace diskpro::win
