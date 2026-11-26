// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
#include "virtual_disk.h"

#include <windows.h>
#include <virtdisk.h>

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace diskpro::win {
namespace {

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(size_t(n), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), out.data(), n) != n) return {};
    return out;
}

bool isVhdx(const std::string& path) {
    auto ext = std::filesystem::u8path(path).extension().u8string();
    std::string s(ext.begin(), ext.end());
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s == ".vhdx";
}

bool supportedExtension(const std::string& path) {
    auto ext = std::filesystem::u8path(path).extension().u8string();
    std::string s(ext.begin(), ext.end());
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s == ".vhd" || s == ".vhdx";
}

VirtualDiskResult fail(const char* operation, DWORD code) {
    VirtualDiskResult r;
    r.error = std::string(operation) + " failed (Windows error " + std::to_string(code) + ")";
    return r;
}

VIRTUAL_STORAGE_TYPE typeFor(const std::string& path) {
    VIRTUAL_STORAGE_TYPE t{};
    t.DeviceId = isVhdx(path) ? VIRTUAL_STORAGE_TYPE_DEVICE_VHDX : VIRTUAL_STORAGE_TYPE_DEVICE_VHD;
    t.VendorId = {0xEC984AEC, 0xA0F9, 0x47E9, {0x90, 0x1F, 0x71, 0x41, 0x5A, 0x66, 0x34, 0x5B}};
    return t;
}

VirtualDiskResult openExisting(const std::string& path, VIRTUAL_DISK_ACCESS_MASK access, HANDLE& handle) {
    handle = INVALID_HANDLE_VALUE;
    if (!supportedExtension(path)) return {false, "only .vhd and .vhdx files are supported"};
    const std::wstring p = wide(path);
    if (p.empty()) return {false, "invalid UTF-8 path"};
    auto type = typeFor(path);
    OPEN_VIRTUAL_DISK_PARAMETERS params{};
    params.Version = OPEN_VIRTUAL_DISK_VERSION_1;
    const DWORD e = OpenVirtualDisk(&type, p.c_str(), access, OPEN_VIRTUAL_DISK_FLAG_NONE, &params, &handle);
    if (e != ERROR_SUCCESS) return fail("OpenVirtualDisk", e);
    return {true, {}};
}

}  // namespace

VirtualDiskResult createVirtualDisk(const std::string& path, uint64_t sizeBytes, bool dynamic) {
    constexpr uint64_t kMinSize = 3ull << 20;
    constexpr uint64_t kMaxSize = 2ull << 40;
    if (!supportedExtension(path)) return {false, "only .vhd and .vhdx files are supported"};
    if (sizeBytes < kMinSize || sizeBytes > kMaxSize || sizeBytes % 512) {
        return {false, "size must be sector-aligned and between 3 MiB and 2 TiB"};
    }
    std::error_code ec;
    const auto file = std::filesystem::u8path(path);
    if (std::filesystem::exists(file, ec)) return {false, "output file already exists"};
    const auto parent = file.parent_path();
    if (!parent.empty() && !std::filesystem::is_directory(parent, ec)) return {false, "output directory does not exist"};
    if (!dynamic) {
        const auto directory = parent.empty() ? std::filesystem::current_path(ec) : parent;
        const auto capacity = std::filesystem::space(directory, ec);
        if (ec) return {false, "cannot determine available space"};
        if (capacity.available < sizeBytes) return {false, "not enough free space for a fixed virtual disk"};
    }
    const std::wstring p = wide(path);
    if (p.empty()) return {false, "invalid UTF-8 path"};

    auto type = typeFor(path);
    CREATE_VIRTUAL_DISK_PARAMETERS params{};
    if (isVhdx(path)) {
        params.Version = CREATE_VIRTUAL_DISK_VERSION_2;
        params.Version2.MaximumSize = sizeBytes;
        params.Version2.PhysicalSectorSizeInBytes = 4096;
        params.Version2.SectorSizeInBytes = 512;
    } else {
        params.Version = CREATE_VIRTUAL_DISK_VERSION_1;
        params.Version1.MaximumSize = sizeBytes;
    }
    const auto flags = dynamic ? CREATE_VIRTUAL_DISK_FLAG_NONE : CREATE_VIRTUAL_DISK_FLAG_FULL_PHYSICAL_ALLOCATION;
    HANDLE h = INVALID_HANDLE_VALUE;
    const DWORD e = CreateVirtualDisk(&type, p.c_str(), VIRTUAL_DISK_ACCESS_NONE, nullptr, flags, 0, &params, nullptr, &h);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    if (e != ERROR_SUCCESS) {
        std::filesystem::remove(file, ec);
        return fail("CreateVirtualDisk", e);
    }
    return {true, {}};
}

VirtualDiskResult attachVirtualDisk(const std::string& path, bool readOnly) {
    HANDLE h = INVALID_HANDLE_VALUE;
    const auto access = readOnly ? VIRTUAL_DISK_ACCESS_ATTACH_RO : VIRTUAL_DISK_ACCESS_ATTACH_RW;
    auto opened = openExisting(path, access, h);
    if (!opened.ok) return opened;
    ATTACH_VIRTUAL_DISK_PARAMETERS params{};
    params.Version = ATTACH_VIRTUAL_DISK_VERSION_1;
    auto flags = ATTACH_VIRTUAL_DISK_FLAG(ATTACH_VIRTUAL_DISK_FLAG_NO_DRIVE_LETTER | ATTACH_VIRTUAL_DISK_FLAG_PERMANENT_LIFETIME);
    if (readOnly) flags = ATTACH_VIRTUAL_DISK_FLAG(flags | ATTACH_VIRTUAL_DISK_FLAG_READ_ONLY);
    const DWORD e = AttachVirtualDisk(h, nullptr, flags, 0, &params, nullptr);
    CloseHandle(h);
    return e == ERROR_SUCCESS ? VirtualDiskResult{true, {}} : fail("AttachVirtualDisk", e);
}

VirtualDiskResult detachVirtualDisk(const std::string& path) {
    HANDLE h = INVALID_HANDLE_VALUE;
    auto opened = openExisting(path, VIRTUAL_DISK_ACCESS_DETACH, h);
    if (!opened.ok) return opened;
    const DWORD e = DetachVirtualDisk(h, DETACH_VIRTUAL_DISK_FLAG_NONE, 0);
    CloseHandle(h);
    return e == ERROR_SUCCESS ? VirtualDiskResult{true, {}} : fail("DetachVirtualDisk", e);
}

}  // namespace diskpro::win
