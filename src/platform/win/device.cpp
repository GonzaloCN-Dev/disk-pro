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
    disk_ = n;
}
DeviceSink::~DeviceSink() {
    for (void* v : locked_) CloseHandle(v);
    if (h_) CloseHandle(h_);
}

// Locks and dismounts every volume that lives (even partly) in [offset, offset+length) of this disk, so that Windows
// does not block the raw write or overwrite the new file system with its cached copy. Handles stay open until destruction.
bool DeviceSink::prepare(uint64_t offset, uint64_t length) {
    if (!h_ || disk_ < 0) return false;
    wchar_t name[MAX_PATH];
    HANDLE find = FindFirstVolumeW(name, MAX_PATH);
    if (find == INVALID_HANDLE_VALUE) return true;
    bool ok = true;
    do {
        std::wstring path = name;
        if (!path.empty() && path.back() == L'\\') path.pop_back();
        HANDLE v = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (v == INVALID_HANDLE_VALUE) {
            v = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);   // query only
            if (v == INVALID_HANDLE_VALUE) continue;
        }
        std::vector<uint8_t> buf(4096);
        DWORD got = 0;
        bool mine = false;
        if (DeviceIoControl(v, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, buf.data(), DWORD(buf.size()), &got, nullptr)) {
            auto* e = reinterpret_cast<VOLUME_DISK_EXTENTS*>(buf.data());
            for (DWORD i = 0; i < e->NumberOfDiskExtents; ++i) {
                const auto& x = e->Extents[i];
                if (int(x.DiskNumber) != disk_) continue;
                uint64_t s = uint64_t(x.StartingOffset.QuadPart), l = uint64_t(x.ExtentLength.QuadPart);
                if (s < offset + length && offset < s + l) mine = true;
            }
        }
        if (!mine) { CloseHandle(v); continue; }
        if (!DeviceIoControl(v, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0, &got, nullptr) ||
            !DeviceIoControl(v, FSCTL_DISMOUNT_VOLUME, nullptr, 0, nullptr, 0, &got, nullptr)) {
            ok = false;
            CloseHandle(v);
            continue;
        }
        locked_.push_back(v);
    } while (FindNextVolumeW(find, name, MAX_PATH));
    FindVolumeClose(find);
    return ok;
}

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

