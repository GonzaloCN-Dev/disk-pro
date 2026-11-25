// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
#include "device.h"
#include <windows.h>
#include <winioctl.h>
#include <string>
#include <vector>

namespace diskpro {

DeviceSource::DeviceSource(int n) {
    if (n < 0 || n > 4095) return;
    std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(n);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DISK_GEOMETRY_EX g{};
    DWORD got = 0;
    if (!DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, &g, sizeof g, &got, nullptr) ||
        g.DiskSize.QuadPart <= 0 || g.Geometry.BytesPerSector == 0) {
        CloseHandle(h);
        return;
    }
    size_ = uint64_t(g.DiskSize.QuadPart);
    sector_ = g.Geometry.BytesPerSector;
    h_ = h;
}

DeviceSource::~DeviceSource() { if (h_) CloseHandle(h_); }

bool DeviceSource::read(uint64_t off, void* buf, size_t len) const {
    if (!h_ || len == 0) return h_ != nullptr;
    if (off > size_ || len > size_ - off) return false;
    uint64_t start = off / sector_ * sector_;
    uint64_t end = (off + len + sector_ - 1) / sector_ * sector_;
    if (end > size_) end = size_;
    std::vector<uint8_t> tmp(size_t(end - start));
    size_t done = 0;
    while (done < tmp.size()) {
        LARGE_INTEGER p; p.QuadPart = LONGLONG(start + done);
        if (!SetFilePointerEx(h_, p, nullptr, FILE_BEGIN)) return false;
        DWORD chunk = DWORD(std::min<size_t>(tmp.size() - done, 1u << 20)), r = 0;
        if (!ReadFile(h_, tmp.data() + done, chunk, &r, nullptr) || r == 0) return false;
        done += r;
    }
    std::memcpy(buf, tmp.data() + (off - start), len);
    return true;
}

DeviceSink::DeviceSink(int n) {
    if (n < 0 || n > 4095) return;
    std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(n);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_WRITE_THROUGH, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DISK_GEOMETRY_EX g{};
    DWORD got = 0;
    if (!DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, &g, sizeof g, &got, nullptr) || g.DiskSize.QuadPart <= 0 ||
        g.Geometry.BytesPerSector == 0) {
        CloseHandle(h);
        return;
    }
    size_ = uint64_t(g.DiskSize.QuadPart);
    sector_ = g.Geometry.BytesPerSector;
    h_ = h;
}
DeviceSink::~DeviceSink() { if (h_) CloseHandle(h_); }

bool DeviceSink::write(uint64_t off, const void* buf, size_t len) {
    if (!h_ || len == 0) return false;
    if (off % sector_ || len % sector_ || off > size_ || len > size_ - off) return false;
    auto* p = static_cast<const uint8_t*>(buf);
    size_t done = 0;
    while (done < len) {
        LARGE_INTEGER pos; pos.QuadPart = LONGLONG(off + done);
        if (!SetFilePointerEx(h_, pos, nullptr, FILE_BEGIN)) return false;
        DWORD chunk = DWORD(std::min<size_t>(len - done, 1u << 20)), w = 0;
        if (!WriteFile(h_, p + done, chunk, &w, nullptr) || w != chunk) return false;
        done += w;
    }
    return true;
}
bool DeviceSink::flush() { return h_ && FlushFileBuffers(h_) != 0; }
void DeviceSink::refreshLayout() { DWORD got = 0; if (h_) DeviceIoControl(h_, IOCTL_DISK_UPDATE_PROPERTIES, nullptr, 0, nullptr, 0, &got, nullptr); }

}  // namespace diskpro

